# Spec: backport de decals de Godot 4 a los binarios de godot-box3d-3

**ESTADO: implementado y validado (F1-F2).** El MVP funciona end-to-end en
GLES3: `patches/zzz_feature_decal_gles3.patch` (motor) + módulo `decal/` (nodo).
Test visual: `DECAL_OK` con albedo + emission + fades + multi-decal sobre
llvmpipe (comando abajo). Pendiente de fases: F3 (normal/ORM/mipmaps) y QA en
hardware móvil real.

Este documento conserva el diseño original para revisión; las desviaciones
respecto al plan están anotadas al final.

Borrador para revisión. Evalúa portar el `Decal` de Godot 4 (proyección
per-píxel de texturas sobre superficies, estilo motor moderno) al renderer
GLES3 de Godot 3.6, con el reparto patch/módulo que este repo ya usa. Cada
punto cita archivo y línea de la fuente verificada: Godot `3.6.4-rc`
(`../godot`) y Godot `master` (`drivers/gles3/shaders/scene.glsl` de 4.x,
ramo Compatibility).

## TL;DR

- **Factible y con molde exacto.** La implementación de decals del renderer
  *Compatibility* de Godot 4 (GLES3) es arquitectónicamente gemela a lo que el
  GLES3 de 3.6 ya tiene para luces: UBO global + lista de índices por objeto +
  loop de fragmento. El port es estructural, no un rediseño.
- **Costo realista: 2.5–4 semanas** para un MVP (albedo + emission + fades +
  cull mask) como patch de motor + módulo con el nodo. Paridad completa
  (normal/ORM/mipmaps) añade ~1 semana.
- **Riesgo principal: presupuesto de texture units en móvil** (ya lo sufrimos,
  ver `patches/README.md`, `gles3_ubershader_sampler_budget.patch`) y el
  **perfil GLES2** de Odisea en Android, donde esta implementación no aplica.
- **Decisión abierta que condiciona todo**: ¿importan los decals en GLES2?
  Si sí, hay una segunda opción (malla proyectada, módulo puro) o una fase
  GLES2 con diseño distinto.

## Cómo funcionan los decals en Godot 4 (referencia)

Dos implementaciones, y solo una nos sirve de molde:

| Renderer | Mecanismo | ¿Portar? |
|----------|-----------|----------|
| Forward+ (Vulkan) | Cluster builder: los decals entran en clusters espaciales y se resuelven en el pase de luz | **No** — no existe cluster builder en 3.x ni hace falta |
| Compatibility (GLES3) | Atlas 2D único + UBO `DecalData` + máscara por objeto + loop en el fragmento | **Sí** — es el modelo |

Del shader de Compatibility de 4.x (`drivers/gles3/shaders/scene.glsl`,
ramo master), verificado hoy:

```glsl
#ifdef USE_DECALS
uniform sampler2D decal_atlas; // texunit:-12   <- UN solo sampler
uniform uint decal_count;
uniform uint decals0;          // máscara por objeto: 2 x u32
uniform uint decals1;

struct DecalData {
    mat4 xform;            // mundo(=vista) -> espacio del decal
    vec3 inv_extents;
    float albedo_mix;
    vec4 albedo_rect;      // rect en el atlas por canal
    vec4 normal_rect;
    vec4 orm_rect;
    vec4 emission_rect;
    vec4 modulate;
    float emission_energy;
    uint mask;             // capas visuales
    float upper_fade, lower_fade;
    mat3x4 normal_xform;
    vec3 normal;
    float normal_fade;
};
layout(std140) uniform DecalDataBlock { DecalData data[MAX_DECALS]; } decal_data_block;
#endif
```

El loop de fragmento (líneas ~2447–2526 del archivo de 4.x) es ~90 líneas:
proyecta `vertex` con `xform`, descarta si sale del box `[0,1]×[-1,1]×[0,1]`,
aplica `upper_fade/lower_fade` sobre Y local, `normal_fade` contra la normal
geométrica, y mezcla albedo (lerp por alpha × albedo_mix), normal, ORM y
emisión (aditiva) leyendo del atlas con `textureLod` (o `textureGrad` con
mipmaps). Los índices llegan empacados a razón de **8 decals por objeto**
(2 u32 con bytes de 8 bits + terminador `0xFF`).

Detalles que conviene copiar tal cual:

- **Un atlas, no texture arrays**: un solo `sampler2D`, sin GL_TEXTURE_2D_ARRAY
  en el path de decals. Clave para el presupuesto de unidades.
- El decal **no es geometría**: no dibuja nada, solo modifica albedo/normal/
  ORM/emisión del fragmento receptor. No participa en depth ni sombras.
- `USE_DECALS` es una especialización: sin decals en escena, el sampler y el
  loop no existen en el binario del shader.

## Por qué encaja limpio en el GLES3 de 3.6

El renderer de 3.6 ya tiene la maquinaria gemela funcionando para luces:

1. **Lista de luces por objeto**, armada por draw en
   `RasterizerSceneGLES3::_setup_light()` (`rasterizer_scene_gles3.cpp:1779`):
   recorre `e->instance->light_instances`, filtra por
   `layer_mask & cull_mask`, y sube `OMNI_LIGHT_COUNT`/`OMNI_LIGHT_INDICES`
   como uniforms del draw.
2. **UBO global** con los datos de todas las luces visibles del frame
   (`state.omni_array_tmp` → `glBufferSubData`, `:3003`), indexado por esos
   índices.
3. **Consumo en el fragmento** por índice (`scene.glsl:2338`:
   `light_process_omni(omni_light_indices[i], ...)`), con los límites
   `MAX_FORWARD_LIGHTS` / `MAX_LIGHT_DATA_STRUCTS` inyectados como custom
   defines (`:5205-5206`).

Un decal es exactamente "otra lista como las luces", con dos diferencias: el
criterio de selección por objeto es overlap de AABB (no intersección de luz) y
el UBO guarda rects de atlas y una matriz, no colores. El shader de 3.6
expone `vertex_interp` (espacio de vista) en el fragmento, igual que 4.x
Compatibility, así que **el loop se porta casi textual**.

## Diseño propuesto

### Reparto patch / módulo (el mismo que usa el repo)

| Pieza | Dónde | Contenido |
|-------|-------|-----------|
| Motor (VS + renderer GLES3 + shader) | `patches/zzz_feature_decal_gles3.patch` | VS API `decal_*`, `INSTANCE_DECAL`, culling, storage/atlas, render_scene, bloque `USE_DECALS` |
| Nodo + editor | módulo nuevo `decal/` en este repo | Nodo `Decal`, gizmo de box, EditorPlugin, icono |

`custom_modules` acepta lista separada por comas (ya se usa así para TheGates:
`scripts/build.sh:199`, `custom_modules="$here,$gates_modules"`), así que un
segundo módulo en el repo no cambia el build. El patch se aplica en el loop
existente (`scripts/build.sh:67`), como los demás.

Convención a decidir en revisión: hoy `patches/` se declara "upstreamable
fixes" (`patches/README.md`). Este patch es una **feature** y no es
upstreamable. Propuesta: sección nueva en el README de patches con ese aviso
explícito, patch separado y autocontenido, revertible con `git apply -R`.
El commit de Godot está pineado en `build.sh`, así que no hay drift real.

### 1. VisualServer (patch: `servers/visual_server.{h,cpp}`)

API paralela al grupo `light_*`:

- `decal_create()`, `decal_set_size()`, `decal_set_texture(DecalTexture, RID)`
  (ALBEDO/NORMAL/ORM/EMISSION), `decal_set_emission_energy()`,
  `decal_set_albedo_mix()`, `decal_set_modulate()`, `decal_set_upper_fade()`,
  `decal_set_lower_fade()`, `decal_set_normal_fade()`, `decal_set_cull_mask()`,
  `decal_set_enable_distance_fade()`, `decal_set_distance_fade()`.
- `INSTANCE_DECAL` en el enum de tipos de instancia; `instance_set_base()`
  acepta un decal RID. El decal se instancia como las luces (transform +
  layers por instancia), así un `Decal` node es un wrapper fino como `Light`.

### 2. Escena (patch: `servers/visual/visual_server_scene.{h,cpp}`)

- `Instance` soporta base tipo decal (AABB derivado de `size`, no de geometría;
  no va a listas de geometría ni de sombra).
- `render_camera()` culla decals aparte (tercer array de resultados, como
  `reflection_probe_cull_result`).

### 3. Firma de `render_scene` (patch: `servers/visual/rasterizer_scene.h`)

Nuevos parámetros `RID *p_decal_cull_result, int p_decal_cull_count`.
Implementaciones a tocar: GLES3 (real), GLES2 y dummy (`drivers/dummy/`):
**stub vacío** en ambas. Los builds `platform=server` (headless de CI) usan
el dummy y quedan intactos funcionalmente.

### 4. Storage + atlas (patch: `drivers/gles3/rasterizer_storage_gles3.*`)

- Estructura `Decal` (RID owner): texturas por canal, size, fades, cull mask,
  flags de dirty.
- **El pedazo más delicado**: el atlas. Un `GL_TEXTURE_2D` RGBA8 global que
  crece; packs por clases de tamaño con free-list; si se queda sin hueco,
  reasigna todo y remapea rects (así lo hace el atlas de 4.x Compatibility).
  Los rects resultantes alimentan `DecalData.albedo_rect` etc. Sin mipmaps en
  el MVP (`textureLod 0`); `USE_DECAL_MIPMAPS` queda para la fase de paridad.

### 5. Pipeline (patch: `drivers/gles3/rasterizer_scene_gles3.{h,cpp}`)

- `SceneState::DecalData` espejo del struct GLSL + `decal_array_tmp`; un UBO
  nuevo (slot libre; 3.6 usa `ubo:3..5` para luces).
- En `render_scene()`: por cada instancia del render list, por cada decal
  visible: test de overlap AABB (la instancia ya trae su AABB transformada) +
  filtro `layer_mask & decal->cull_mask` → máscara de índices. Cuando un
  objeto supera 8 decals, se prioriza por cercanía a cámara (regla propia;
  documentarla).
- `_setup_decals(e)` hermano de `_setup_light()` (`:1779`): sube
  `DECAL_COUNT` + máscara por draw.
- Uso condicional: solo si `p_decal_cull_count > 0` se activa la
  especialización `USE_DECALS` — sin decals, ni sampler ni loop ni variantes.

### 6. Shader (patch: `drivers/gles3/shaders/scene.glsl`)

- Bloque `USE_DECALS` del struct/UBO/máscara, portado casi textual del de 4.x
  (GLSL ES 3.00 soporta los uint/bitwise/`textureLod` que usa).
- Colocación: tras el fetch de material y antes del lighting del path
  per-pixel (`:1345` zona). Fuera de `MODE_RENDER_DEPTH` y sombras.
- MVP sin canal normal: sin `normal_xform`/`normal_fade` ni varying extra de
  normal geométrica (la paridad la necesita; ver fases).

## Presupuesto y riesgos

1. **Texture units (el riesgo que ya conocemos).** El patch
   `gles3_ubershader_sampler_budget.patch` dejó el ubershader en 9 unidades de
   motor y 7 de material sobre 16 (Adreno). `decal_atlas` añade 1: motor 10,
   material 6. Un `SpatialMaterial` full (albedo+normal+ORM+AO+emission) pide
   5–6: **entra, sin margen**. Mitigaciones: `USE_DECALS` solo se compila con
   decals en escena (proyectos sin decals no pagan nada), y el interplay con
   `gles3_async_compile_queue_fallback.patch` (el compile síncrono de
   fallback) ya está endurecido por experiencia propia.
2. **Variantes**: un eje nuevo `USE_DECALS` duplica variantes *solo en escenas
   con decals*. El ubershader actual no incluye decals; el primer frame con un
   decal nuevo compila una variante síncrona (hitch puntual en móvil; aceptable
   para MVP, re-evaluar si el ubershader debe llevar el bloque).
3. **GLES2 queda sin decals** en esta implementación. GLES2 no tiene UBOs ni
   listas por objeto (su renderer dibuja un pase por luz). Un decal en GLES2
   necesitaría otro diseño (pase aditivo por decal, tipo como GLES2 ya itera
   luces) — fase aparte, estimada abajo. **Dato que fuerza la decisión**:
   el perfil móvil de referencia de Odisea (Redmi Note 9 Pro) corre GLES2
   (`docs/odisea-box3d.md`).
4. **Alcance del patch**: toca ~10 archivos del motor con una feature nueva.
   Contra 3.6.4-rc pineado el mantenimiento es bajo; si algún día se rebasea,
   este patch es el que más roce va a dar.
5. **WebGL2**: sin blockers (UBO, uint bitwise y `textureLod` existen en
   GLSL ES 3.00); verificar el presupuesto de units en ANGLE.
6. **Semántica visual**: los decals modifican albedo/normal/ORM *antes* del
   lighting (mismo pase, no post-proceso), así que reciben sombras y fog como
   la superficie — es el comportamiento de Godot 4 y el que se espera.

## Esfuerzo estimado

| Pieza | Est. |
|-------|------|
| VS API + `INSTANCE_DECAL` + cull en escena | 2–3 d |
| Firma `render_scene` + stubs GLES2/dummy | 0.5 d |
| Storage de decal + atlas con free-list | 3–4 d |
| Pipeline: masks, UBO, `_setup_decals` | 2–3 d |
| `scene.glsl` bloque MVP | 1–2 d |
| Nodo `Decal` + gizmo + editor (módulo `decal/`) | 2–3 d |
| Integración con patches de units/async + QA móvil | 2–4 d |
| **Total MVP** | **12–19 d (~2.5–4 semanas)** |
| Paridad: canales normal/ORM, `normal_fade` (varying geo-normal), mipmaps | +4–6 d |
| (Opcional) GLES2 por pase aditivo por decal | +5–10 d |

## Alternativa sin patches (Opción B): malla proyectada

Si la prioridad es "cero patches de feature" o GLES2 importa:

- Nodo `Decal` en un módulo que **genera geometría**: proyecta el quad del
  decal sobre las superficies cercanas (recortando el quad contra las caras
  cercanas) y lo dibuja como mesh propio con `poly offset` contra z-fighting.
- Sin cambios de motor; funciona en GLES2 y GLES3 por igual; la calidad es la
  de los addons clásicos de decals de Godot 3: bien para marcas estáticas
  (agujeros de bala, manchas) sobre geometría estática, sin proyección
  per-píxel sobre superficies que se deforman ni canales de material.
- Sinergia disponible: las queries del BVH de Box3D pueden servir para
  encontrar las superficies a proyectar.

Fidelidad claramente inferior al port nativo; es el plan B.

## Plan de pruebas

1. **Smoke headless** (`scripts/test.sh`): escena nueva `decal_api_smoke` —
   crea un `Decal`, setea textura/size/fades, mueve, libera; sin `ERR_PRINT`.
   Compila en `platform=server` vía dummy (la API vive en VisualServer).
2. **Visual** (`test_project/`): plano + caja con un decal alfa y otro
   emisivo; binario x11/FRT; screenshot y comparación por tolerancia.
3. **Presupuesto móvil**: en Adreno 618, `SpatialMaterial` full + decal activo
   — validar link del ubershader (el fallo conocido de units) y medir el
   sweep 1/8/32 decals (delta de ms/frame).
4. **WebGL2**: template html5, misma escena.

## Fases

1. **F1 — API + atlas sin shader**: VS API, storage, atlas, nodo y gizmo
   funcionan en el editor (se ve el AABB, las rects del atlas se debuguean).
   Criterio: atlas estable con add/remove repetido, sin leaks de slices.
2. **F2 — MVP visual GLES3**: bloque `USE_DECALS` con albedo+emission+fades+
   cull_mask. Criterio: prueba visual 2 pasa; sin decals, cero variantes
   nuevas (verificar con `INFO_SHADER_COMPILES_IN_FRAME`).
3. **F3 — Paridad de canales**: normal, ORM, `normal_fade`, mipmaps
   (`USE_DECAL_MIPMAPS`), distance fade.
4. **F4 (decisión aparte) — GLES2** por pase aditivo, solo si el perfil GLES2
   de Odisea lo justifica.

## Puntos abiertos para la revisión

1. ¿El perfil GLES2 en producción de Odisea justifica Opción B o F4, o los
   decals son feature de builds GLES3 (desktop/TheGates/Web)?
2. ¿El nodo vive en módulo (`decal/`) o dentro del patch (`scene/3d/`)?
   Recomiendo módulo: mantiene la feature fuera del diff de motor salvo lo
   que obligatoriamente vive en él (VS + renderer).
3. Presupuesto de units: ¿se acepta material 6 unidades en escenas con decals,
   o se exige degradar decals a albedo-only cuando el material pide 6?
4. Regla de prioridad a 8 decals por objeto: cercanía a cámara (propuesta)
   vs. orden de creación vs. prioridad editable.

## Notas de la implementación (lo que quedó)

Desviaciones y decisiones tomadas durante la implementación:

1. **Atlas con readback GL, no imágenes retenidas.** El plan asumía las
   `Ref<Image>` del `Texture` (`texture->images`), pero `keep_original_textures`
   solo está activo en el editor (`editor_node.cpp:5887`); en un juego
   exportado no hay CPU data. El atlas hace `texture_get_data()` (readback GL)
   una vez por textura nueva — funciona igual en editor y runtime.
2. **Atlas `GL_RGBA8` + `decal_srgb_to_linear()` en el shader** (el esquema de
   4.x Compatibility), no `GL_SRGB8_ALPHA8` con decode por hardware: los bytes
   del atlas son los crudos de la textura fuente y el shader decodifica.
   Nota: leer de vuelta una textura con flag sRGB puede devolver bytes
   decodificados según el driver; si se detecta en móvil, la alternativa es
   rehacer el readback vía FBO blit.
3. **Slots por objeto como floats**: `uniform vec4 decal_slots0/1` con bytes
   1-based (0 = terminador). `ShaderGLES3::set_uniform(uint32_t)` baja como
   `glUniform1i`, incompatible con un uniform `uint` de GLSL, así que no hay
   máscaras de bits en el shader.
4. **Los decals de la máscara son 1-based** (`uint64_t(j+1) << (slot*8)`) para
   no colisionar con el terminador 0.
5. **texunit:-14 y `//ubo:7`** para el atlas y el `DecalDataBlock`.
6. **`render_scene` recibe `InstanceBase **p_decal_cull_result`** (punteros a
   instancias del cull, no RIDs de instancias de renderer): los decals no
   necesitan `base_data` ni instancias del lado del renderer.
7. **Máscaras por objeto calculadas en `_prepare_scene`** (AABB overlap +
   `decal_get_cull_mask`), ordenadas por distancia a cámara; la máscara vive en
   `RasterizerScene::InstanceBase::decal_mask` (nuevo campo, patch en
   `rasterizer.h`).
8. **Distance fade** se dobla en `modulate.a` por frame (CPU), no en shader.
9. **No afecta a depth/shadow passes** (`USE_DECALS` se apaga con `p_shadow`).
10. **Uso en escenas**: `Decal` proyecta a lo largo de su `-Y` local
    (semántica de Godot 4): plano de textura en X/Z, eje de grosor en Y. Con
    `size.y` delgada y el decal apoyado en la superficie, el fade tiende a 1.

### Test de aceptación (visual, requiere display GL)

```sh
xvfb-run -a -s "-screen 0 1024x600x24" \
  bin/godot.x11.tools.64 --path <proyecto> -s test_decal.gd
```

El script arma plano + decals de prueba, captura el viewport y compara
colores (imprime `DECAL_OK`). Referencia guardada en `docs/decal-backport-spec.md`
historia; el test vive en `/tmp/kilo/decal-test/` hasta migrarse a
`test_project/`.

### Deuda conocida (F3+)

- Canales normal/ORM reservados pero sin wire (struct listo, rects en 0).
- Los slices del atlas no se liberan cuando una textura de decal se borra
  (solo memoria GL residual; los rects quedan si la textura reaparece).
- El ubershader no lleva el bloque de decals: el primer frame con decals
  compila una variante síncrona (mitigado por los patches de async fallback).
- Compressed textures rechazadas con warning.
- GLES2: el nodo existe y no renderiza (por diseño, F4 si hace falta).
- Ocultar un `Decal` (`visible = false`) saca de la lista de decals a *todos*
  los demás: los `decal_mask` por objeto se construyen contra el índice
  (`j + 1`) del array de la cull, y al desaparecer uno los índices se corren
  sin rehacer las máscaras, así que los slots apuntan a decals equivocados o a
  cero. Reproducido en `decal/demo_advanced` (modo `DECAL_DEMO_SHADOW=decal`)
  con el decal de piso oculto: desaparecen las quemaduras y el láser aunque
  sigan visibles. Workaround en la demo: dejarlo visible con `modulate.a = 0`.
  El arreglo real es reconstruir las máscaras después de mover/ocultar decals
  (o reindexar en `_setup_decals`).
