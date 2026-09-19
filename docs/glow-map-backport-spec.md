# Spec: glow map (Godot 3.x PR #93133) en godot-box3d-3

Estado: **implementado** en `patches/zzzzz_feature_glow_map_gles3.patch`.

Feature: una textura `glow_map` multiplica el glow resultante del post-proceso,
modulada por `glow_map_strength` — el efecto "lens dirt" de Godot 4. Es render
puro sobre el rasterizador GLES3; no toca física ni el módulo `box3d/`.

Fuente verificada: PR [`godotengine/godot#93133`](https://github.com/godotengine/godot/pull/93133)
(`Chaosus`, mergeado 2024-09-24 por `lawnjelly`, commit
`f70472f1ccb10dd25f83cbecb0be571bc8ea8cbd`, milestone 3.7, +133/−19, 15
archivos) y el checkout `../godot` (branch 3.6, pin `3.6.4-rc`, patches del repo
ya aplicados encima).

## TL;DR

- **Feature de render, sólo GLES3.** El mapa se muestrea en `tonemap.glsl`
  (`glow = mix(glow, texture(glow_map, …).rgb * glow, glow_map_strength)`), no
  en el shader de escena: no consume bits del presupuesto de 31 condicionales
  del ubershader que ya nos mordió con decal y blob shadows.
- **GLES2 lo ignora por diseño**, y el propio doc del PR lo aclara: el
  `environment_set_glow_map` de `RasterizerSceneGLES2` es un no-op. La API
  existe en todos los rasterizadores (incluido el dummy) para que la propiedad
  scene-side no rompa nada.
- **Se aplica limpio sobre 3.6.4-rc** con todos nuestros patches encima; el
  único cambio de fondo es de unidades de textura (ver Desviaciones).
- **Costo en móvil:** el fetch extra del mapa se paga siempre que el glow esté
  activo y `glow_map_strength > 0.001` (el branch del shader), pero el código
  mergeado no agrega un `#define` propio: se activa dentro de `USING_GLOW`, así
  que no cambia la cantidad de variantes del tonemap. Para Odisea el caso típico
  (sin mapa) no llega al fetch porque `_update_glow_map()` manda `strength = 0`
  cuando no hay textura.

## Qué toca el PR

| Área | Archivos | Riesgo para nosotros |
|------|----------|----------------------|
| API de `Environment` | `scene/resources/environment.{h,cpp}` — `glow_map`, `glow_map_strength`, `_update_glow()`/`_update_glow_map()`, `_bind_methods()` | Bajo. Ningún patch nuestro toca `environment.*`. |
| API de `VisualServer` | `servers/visual_server.{h,cpp}`, `visual_server_raster.h`, `visual_server_wrap_mt.h`, `rasterizer.h` | Bajo. `BIND3(environment_set_glow_map, RID, float, RID)` y su `FUNC3` espejo. |
| GLES3 | `drivers/gles3/rasterizer_scene_gles3.{h,cpp}`, `drivers/gles3/shaders/tonemap.glsl` | Medio. Comparte archivos con decal/diagnostics/blob shadows, pero los hunks caen en `environment_set_glow()` y `_post_process()`, no en `_render_list()`. |
| GLES2 | `drivers/gles2/rasterizer_scene_gles2.{h,cpp}` | Bajo: no-op documentado. |
| Dummy / docs | `drivers/dummy/rasterizer_dummy.h`, `doc/classes/Environment.xml`, `doc/classes/VisualServer.xml` | Bajo. |

## Unidades de textura del tonemap

El shader de tonemap tenía `source:0`, `source_auto_exposure:1`,
`source_glow:2`, `color_correction:3`. El patch agrega `glow_map:3` y mueve
`color_correction` a `4`, en el GLSL y en el binding
(`WRAPPED_GL_ACTIVE_TEXTURE(GL_TEXTURE4)`). La auditoría del resto de
`_post_process()` confirma que no queda ningún otro uso de unidades con
solapamiento: el filo de BCS/adjustments bindea `color_correction`, y el bloque
de glow del tonemap bindea `source_glow` (2) y `glow_map` (3) justo antes del
draw. El shader de tonemap es de samplers fijos (no es el ubershader), así que
el presupuesto de condicionales no aplica.

## Desviaciones respecto del PR

1. **`glow_map_strength` sin inicializar en el rasterizer.** El PR agrega el
   campo al `struct Environment` de `RasterizerSceneGLES3` pero no a su
   constructor de lista de inicialización, y el constructor del resource
   (`Environment::Environment()`) fija el miembro global en `0.8f` sin llamar a
   `_update_glow_map()`. Resultado upstream: hasta que algo llame a un setter,
   el rasterizer lee el float con basura; si cae en `> 0.001` y hay glow, el
   shader muestrea el texunit 3 sin textura atada. Lo inicializamos en `0.0`
   (`glow_map_strength(0.0),` en la lista), que es el estado correcto de "sin
   mapa". El miembro del resource sigue en 0.8: al asignar un mapa,
   `set_glow_map()` resuelve `_update_glow_map()` y sincroniza el valor.
2. **Nombre y orden.** `zzzzz_feature_glow_map_gles3.patch`: se aplica después
   de blob shadows (que ya ocupa `zzzzz_feature_blob_shadow_gles3.patch`) y
   antes de `zzzzz_frt_frame_profiler.patch`. No hay colisión de hunks —el
   profiler toca `main/main.cpp` y glow no— pero el patch se generó sobre el
   estado que incluye blob shadows, así que ése es el punto de aplicación
   verificado.
3. **Nada del resto se adaptó.** El diff del PR aplica sobre 3.6.4-rc tal cual;
   los nombres/estructuras de `TonemapShaderGLES3` son autogenerados por
   `gles_builders.py` a partir de los `uniform`, así que no hace falta tocar
   `tonemap.glsl.gen.h`.

## Tests

- `test_project/tests/m30_glow_map_api.{gd,tscn}` (headless, `scripts/test.sh`):
  binding de `VisualServer.environment_set_glow_map`, default del resource
  (`null` + `0.8`), ida y vuelta de textura/strength, limpieza del mapa y
  reassign; corre igual en el rasterizador dummy (server build) y GLES2.
- `test_project/glow_map_visual.gd` (necesita GL real): monta fondo oscuro +
  quad emisivo con glow fuerte, captura un frame con un `glow_map` negro
  (strength 1.0, apaga el glow) y otro sin mapa, y compara el halo contra una
  zona de control.

  ```bash
  xvfb-run -a -s "-screen 0 1024x600x24" \
    ../godot/bin/godot.x11.opt.tools.64 --path test_project \
    --video-driver GLES3 -s glow_map_visual.gd     # GLOW_OK
  ```

  Con `--video-driver GLES2` el test espera que con y sin mapa sean iguales
  (el rasterizador lo ignora) e imprime `GLOW_GLES2_OK`.

## Deuda conocida

- El mapa se estira a pantalla completa: la doc recomienda una textura con el
  aspect ratio del proyecto.
- GLES2 no tiene el efecto (decisión upstream; su glow es una implementación
  simple para equipos de gama baja).
- Sin `#define USING_GLOWMAP`: el mapa se activa con el `USING_GLOW` existente,
  así que con glow activo el branch y el sampler existen aunque no haya mapa
  (el estado sin mapa se resuelve con `strength = 0`, no con una variante
  aparte).