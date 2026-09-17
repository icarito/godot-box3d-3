# Spec: backport de Blob Shadows (Godot 3.7-dev1) a godot-box3d-3

Borrador para revisión, sin implementar. Evalúa portar `BlobShadow`/`BlobFocus`
(sombras suaves analíticas de esfera/cápsula, nuevas en la rama `3.x` de Godot,
lanzadas como parte de 3.7-dev1) sobre nuestro pin de Godot `3.6.4-rc` con los
patches GLES3 que ya cargamos (decal, thread-safety, shader cache).

> **Implementado.** El puerto vive en
> `patches/zzzzz_feature_blob_shadow_gles3.patch` (GLES3 y GLES2) y se aplica
> último. Las decisiones y desviaciones respecto de este borrador —sobre todo
> el presupuesto de conditionals, la adaptación a la interpolación de 3.6 y el
> arreglo del camino MRT— están en
> [Estado de la implementación](#estado-de-la-implementación-implementado).

Fuente verificada: PR [`godotengine/godot#84804`](https://github.com/godotengine/godot/pull/84804)
(`lawnjelly`, mergeada 2025-10-09, +3348/-64, 46 archivos), estado actual del
checkout `../godot` (branch `3.6`, patches del repo ya aplicados encima) y
`patches/README.md` de este repo.

## TL;DR

- **Feature real, no cosmética.** Sombra suave analítica por fragment shader
  (sin shadow map), pensada para un número acotado de objetos dinámicos sobre
  lightmaps estáticos — exactamente el perfil de Odisea (personajes/props
  moviéndose sobre geometría bakeada).
- **No toca física.** Es puramente render: dos nodos `Spatial` nuevos
  (`BlobShadow`, `BlobFocus`), UBO en el `VisualServer`, loop de fragmento en
  `scene.glsl`. Cero interacción con `box3d/` — confirmado, no hay ninguna
  referencia real a esto en el módulo (los hits de `grep -i blob` en
  `core/object.h`/`donors.gen.h` son falsos positivos, coincidencias de
  substring, no relacionados).
- **Riesgo concreto y ya conocido por nosotros: el presupuesto de 31
  conditionals del ubershader de `scene.glsl`.** Ver más abajo — es el mismo
  problema que ya nos mordió con el decal.
- **Costo estimado: 1.5–2.5 semanas** para un patch equivalente (GLES3 solo;
  GLES2 se evalúa aparte, ver Decisión abierta).

## Qué toca el PR upstream

| Área | Archivos | Líneas | Riesgo para nosotros |
|------|----------|--------|----------------------|
| Nodos de escena (`BlobShadow`, `BlobFocus`) | `scene/3d/blob_shadow.{h,cpp}`, `scene/3d/blob_focus.{h,cpp}`, `scene/register_scene_types.cpp` | ~434 | Bajo — código nuevo, sin overlap con nada nuestro |
| VisualServer | `servers/visual/visual_server_blob_shadows.{h,cpp}`, `visual_server_scene.{h,cpp}`, `visual_server_wrap_mt.h`, `visual_server.{h,cpp}` | ~1230 | Medio — `visual_server_scene.cpp` es donde vive la interpolación física (ver spec de abajo); hay que revisar que ambos features no se pisen si algún día se portan juntos |
| `Light` (nueva sección de sombra blob por luz) | `scene/3d/light.{h,cpp}` | ~209 | Bajo |
| Editor (gizmos, iconos) | `editor/spatial_editor_gizmos.{h,cpp}`, iconos SVG | ~142 | Bajo |
| **`drivers/gles3/shaders/scene.glsl`** | 1 archivo | **+240** | **Alto — ver abajo** |
| `drivers/gles2/shaders/scene.glsl` (paridad GLES2) | 1 archivo | +241 | Alto si se quiere GLES2 también |
| `drivers/gles3/rasterizer_scene_gles3.cpp` + storage | varios | ~60 | Medio — mismos archivos que toca `gles3_storage_thread_safety.patch` |
| Core (`fixed_array.h`, `camera_matrix.*`) | 3 | ~130 | Bajo, aditivo |

## El riesgo real: presupuesto de conditionals del ubershader (CONFIRMADO)

Ya lo documentamos nosotros mismos en `patches/README.md`
(`zzz_feature_decal_gles3.patch`): `gles_builders.py` convierte **cada**
`#ifdef` que encuentra en `scene.glsl` en un bit del `VersionKey` del
ubershader, y ese shader está **al límite: 31 conditionals**. Nuestro propio
decal ya empujó `SHADELESS` al bit 31 (`UBERSHADER_FLAG`) y tuvimos que
reescribir sus guards como `#if defined(...)` en vez de `#ifdef` para no
gastar presupuesto.

Leí el diff real del PR (`https://github.com/godotengine/godot/pull/84804.diff`,
sección `drivers/gles3/shaders/scene.glsl`, hunk en la línea original 934) y
**el problema es real, no hipotético**: el feature completo va detrás de
`#ifdef USE_BLOB_SHADOWS` (declarado 3 veces en el archivo: la función
`blob_shadows_multi_shadow()`, su invocación en `main()`, y la multiplicación
final `frag_color.rgb *= blob_shadow_total;`). A diferencia de su vecino
inmediato en el mismo archivo — `#ifdef USE_CONTACT_SHADOWS //ubershader-skip`,
dos líneas después del bloque de blob shadows — **`USE_BLOB_SHADOWS` no lleva
ningún comentario `//ubershader-skip` ni `//ubershader-runtime`**. Eso quiere
decir que, tal como viene el PR, consume un bit más del `VersionKey` de 32
bits — el mismo presupuesto que nuestro decal ya llenó. Aplicado sin
modificar, sobre nuestro fork (no sobre Godot upstream limpio, que no carga
el decal), esto muy probablemente rompe la compilación del ubershader: el
mismo bug que forzó el rewrite de guards en el decal.

Funcionalmente, en cambio, **no hay cruce con el decal**: el decal modifica
el albedo antes de la iluminación (nuestros hunks en `scene.glsl` viven en
las líneas originales ~1664 y ~1991-2026, en medio del cálculo de luces);
blob shadows multiplica el `frag_color.rgb` ya resuelto, al final del todo
(línea original ~2451, justo antes del output). Se componen bien: una
superficie con decal puede recibir sombra de blob normalmente, sin pisarse
ni requerir coordinación de datos entre ambos. Tampoco hay solapamiento de
línea a nivel de patch — los hunks de blob shadows (327/436/699/882/934/1902/2451)
no tocan los mismos rangos que los del decal (1664, 1991-2026), así que
aplicar ambos patches en secuencia no debería fallar por contexto.

Mitigación necesaria: reescribir las 3 apariciones de `#ifdef USE_BLOB_SHADOWS`
/ `#endif` como `#if defined(USE_BLOB_SHADOWS)` (igual que hicimos con
`ENABLE_AO` en el decal) para que no consuma un bit del `VersionKey`. Es un
cambio mecánico y ya sabemos exactamente dónde va — no requiere re-diseño.

## Decisión abierta

¿Importa GLES2? El PR trae paridad GLES2 (241 líneas en su `scene.glsl`, sin
el problema de presupuesto porque GLES2 no usa el mismo esquema de ubershader
con bits). Si Odisea todavía shippea perfil GLES2 en Android de gama baja
(como el decal, que expresamente lo dejó fuera — "GLES2 y el rasterizador
dummy ignoran los decals"), blob shadows en GLES2 es low-risk y podría
portarse primero, antes que GLES3, para validar el diseño sin pelear con el
presupuesto de conditionals.

## Plan sugerido (si se aprueba)

1. Puerto GLES2 primero (bajo riesgo, valida nodos + VisualServer + UBO).
2. Puerto GLES3: auditar cada `#ifdef` nuevo en `scene.glsl` contra el
   presupuesto de 31 conditionals; convertir a `#if defined()` los que no
   necesiten ser un bit del `VersionKey` en runtime.
3. Test visual estilo el que ya usamos para decal (`docs/decal-backport-spec.md`,
   comando de captura en `test_project/`) comparando con/sin blob shadows
   sobre una escena con lightmap estático + un personaje moviéndose.
4. QA en el perfil mobile/FRT donde vive `gles3_ubershader_sampler_budget.patch`
   — confirmar que blob shadows no reintroduce el problema de sampler units
   que ya resolvimos ahí (el PR no declara samplers nuevos según el resumen
   de archivos, pero falta confirmarlo).

## No cubierto por este documento

- El diff línea por línea de `scene.glsl` (el resumen de `gh pr view` da
  archivos y conteos, no el contenido) — falta antes de estimar el trabajo de
  punto 2 con precisión.
- Multimesh/partículas: fuera de alcance del PR mismo (lo dice su descripción).

## Estado de la implementación (implementado)

Puerto completo sobre `patches/zzzzz_feature_blob_shadow_gles3.patch`: nodos
`BlobShadow`/`BlobFocus`, API `blob_shadow_*`/`capsule_shadow_*`/`blob_light_*`
en `VisualServer`, el sistema `VisualServerBlobShadows` (BVH, foco, fundidos),
los parámetros `blob_shadow_*` de `Light`, los ajustes
`rendering/quality/blob_shadows/*` y el bloque `USE_BLOB_SHADOWS` en los dos
`scene.glsl` (GLES3 y GLES2). GLES2 se portó también: el PR lo trae y no tiene
el problema de presupuesto de bits.

Decisiones y desviaciones respecto de este documento:

1. **Presupuesto de condicionales: se liberan bits, no se evita el bit.** La
   mitigación propuesta (escribir `#if defined(USE_BLOB_SHADOWS)`) deja la
   macro sin definir: a diferencia de `ENABLE_AO`, `USE_BLOB_SHADOWS` tiene
   que ser un bit del `VersionKey` para poder alternarse por draw (una misma
   escena dibuja superficies con y sin sombra). Lo que sí era cierto es que el
   shader estaba al tope. La solución es liberar los dos bits que
   `gles3_ubershader_sampler_budget.patch` estaba gastando de más:
   `GI_PROBES_AVAILABLE` y `RADIANCE_MAP_ARRAY_AVAILABLE` son custom defines
   de arranque (`add_custom_define`), nunca pasan por `set_conditional`, así
   que sus guards pasan a `#if defined(...)` y dejan de contarse como
   condicionales. GLES3 queda en 30 (antes 31), con `SHADELESS` en el bit 29 y
   `USE_BLOB_SHADOWS` en el bit 8, más el `static_assert(CONDITIONALS_MAX < 64)`
   que agrega el PR en `gles_builders.py`; GLES2 en 37 (límite 64).
2. **Adaptación de la API de 3.7.** El PR se apoya en la interpolación de paso
   fijo que 3.6 no tiene (`fti_update_servers_xform()`,
   `_get_cached_global_transform_interpolated()`). Los nodos usan
   `NOTIFICATION_TRANSFORM_CHANGED` + `get_global_transform_interpolated()`:
   la sombra sigue al caster por tick de física, no interpolada entre ticks.
3. **El patch se aplica último (`zzzzz_`).** El spec dice que no hay
   solapamiento de hunks, y es cierto en `scene.glsl`, pero no en
   `drivers/gles3/rasterizer_scene_gles3.cpp`: el bloque de uniforms de blob
   shadows va justo después de `set_uniform(WORLD_TRANSFORM, …)` en
   `_render_list()`, que es donde `zzzz_frt_render_diagnostics.patch` insertó
   el bloque `FRT_SKIN_NO_DEPTH`. Si se aplicara antes, el de diagnóstico no
   encontraría contexto.
4. **Camino MRT corregido (no está en el PR).** El PR sólo multiplica
   `frag_color.rgb`, que es la rama sin MRT. En GLES3, SSAO/SSR/DOF/SSS activan
   MRT y el color se acumula en `diffuse_buffer`/`specular_buffer`, así que sin
   multiplicar también esos buffers las sombras desaparecen en cuanto el nivel
   prende SSAO. El patch lo hace y el test lo cubre con `BLOB_SSAO=1`.

Tests:

- `test_project/tests/m29_blob_shadow_api.tscn` (headless, `scripts/test.sh`):
  ciclo de vida de los RID, cambio esfera↔cápsula, ida y vuelta de
  radio/offset, alta/baja de la luz.
- `test_project/blob_shadow_visual.gd` (necesita GL real):
  `xvfb-run -a -s "-screen 0 1024x600x24" ../godot/bin/godot.x11.opt.tools.64
  --path test_project -s blob_shadow_visual.gd` imprime `BLOB_OK`. Medido en
  llvmpipe: GLES3 `delta≈0.42`, GLES3+SSAO (MRT) `delta≈0.30`, GLES2
  `delta≈0.19`, con la zona de control sin cambios.
- `decal/demo_advanced` usa la BlobShadow real por defecto
  (`DECAL_DEMO_SHADOW=blob|decal|both`) y su autoverificación
  (`DECAL_DEMO_AUTO=1`) da `DEMO_OK` en los tres modos; la sombra real es la
  más oscura (luma 0.10 bajo la caja contra 0.35 del piso).

Deuda conocida:

- Sin interpolación suave entre ticks de física (punto 2); con
  `physics/common/physics_fps` bajo la sombra puede notarse a saltos.
- La demo destapó un bug **del port de decals** (no de este patch): ocultar un
  `Decal` saca a todos los demás de la lista. Documentado en
  `docs/decal-backport-spec.md`; la demo lo esquiva dejando el decal de piso
  con `modulate.a = 0`.
- `blob_shadow_shadow_only` ilumina la sombra pero oculta la luz real: en un
  nivel real hay que agregar una luz aparte (como hace el test).
- **Auto-sombreado del caster.** La `BlobShadow` es un volumen analítico:
  oscurece todo lo que tapa, incluida la propia malla del caster, y la API no
  tiene exclusión propia (tampoco upstream). Un occluder centrado dentro del
  mesh y más ancho que él marca los costados y la cara de abajo como una banda
  suave: en la demo (esfera r=0.5 dentro de un cubo de 0.8) bajaba la cara
  frontal hasta luma 80. Ubicado en la base (r=0.45, el mínimo del caster
  quedó en 105) queda como sombra de piso, que es el uso esperado. Los
  personajes hacen lo mismo: el occluder va en los pies, no envolviendo el
  cuerpo, y las piernas se oscurecen un poco, que es deseable.

