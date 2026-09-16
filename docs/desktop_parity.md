# FRT desktop parity — plan por fases

## Context

El `frt-editor` (SDL2, x86_64, `tools=yes`) es hoy el editor de un target ROCKNIX/PortManager arm64,
pero corre con un contexto **OpenGL ES** pedido a Mesa. Eso lo mete por un camino de driver
distinto al de cualquier otro editor de escritorio, y ahí vive el crash de gallium que queda pendiente.
Pedir un contexto **desktop GL 3.3 core** en su lugar debería hacerlo desaparecer por construcción:
es el mismo camino que `platform/x11` ejerce desde hace años.

Además faltan piezas de paridad de escritorio (clipboard, IME, cursores, drag&drop). Dos de las
siete de la lista original **ya no son trabajo** — ver "Hallazgos que cambian el alcance".

Orden pedido: desktop-GL primero; cursores y drag&drop a la cola.

---

## Hallazgos que cambian el alcance

Verificados contra el árbol, no de memoria:

1. **Window icons: ya está hecho.** `platform/frt/frt_godot.cc:329-336` → `sdl2_adapter.h:526-535`
   (duplicate → `FORMAT_RGBA8` → `SDL_CreateRGBSurfaceWithFormat` → `SDL_SetWindowIcon`). Sale del plan.

2. **Fullscreen mode list: no hay nada que portar.** `get_fullscreen_mode_list()` y `set_video_mode()`
   están **vacíos también en x11** (`platform/x11/os_x11.cpp:1168-1176`) **y en Windows**
   (`os_windows.cpp:1984-1988`). FRT ya los tiene vacíos (`frt_godot.cc:247-253`) — o sea, ya está a la par.
   Lo que sí funciona upstream es `set_window_fullscreen()`, que FRT también implementa
   (`frt_godot.cc:271-277`). Sale del plan; queda como nota en `patches/README.md`.

3. **Desktop-GL NO es cambiar el atributo de SDL.** El interruptor real del motor es el define
   `GLES_OVER_GL`, consumido en ~40 sitios de `drivers/gles3/*` — el decisivo es
   `drivers/gles3/shader_gles3.cpp:591-596`, que emite `#version 330` en vez de `#version 300 es`.
   Lo define `drivers/gl_context/SCsub:18` junto con `GLAD_ENABLED`, sólo para
   `["haiku","osx","windows","x11"]`. El atributo de SDL es una consecuencia, no la causa.

4. **El punto que hace o rompe la fase 1:** `RasterizerGLES3::is_viable()` **no tiene ningún call site
   en FRT**. Lo llaman x11, windows, osx, android, iphone, javascript, uwp — FRT no
   (`frt_godot.cc:98-110` va directo a `register_config()` + `make_current()` tras
   `frt_resolve_symbols_gles3()`). Y `gladLoadGL()` vive **dentro** de `is_viable()`
   (`rasterizer_gles3.cpp:139`). Si sólo se cambian los headers a glad y se borra la llamada del
   wrapper `dl/`, **todos los punteros de GL quedan en null** y hay segfault en el primer draw —
   con la misma pinta que el crash de gallium que estamos persiguiendo.

5. **`gladLoadGL()` es el loader equivocado bajo SDL.** `thirdparty/glad/glad.c:94-122`:
   `dlopen("libGL.so.1")` y resuelve por `glXGetProcAddressARB`. Con SDL sobre Wayland/EGL eso no es
   el backend del contexto. En esta máquina funciona por accidente (libglvnd despacha bien), pero se
   rompe en un contenedor o en una Mesa sin glvnd — exactamente el caso ROCKNIX que ya documenta
   `scripts/build.sh`. Hay que usar `gladLoadGLLoader(SDL_GL_GetProcAddress)`.

6. **La Fase 2 se implementó y se revirtió: el desktop-GL tiene que renderizar como el device.** El
   `_check_internal_feature_support()` sin `mobile` bajo `GLES_OVER_GL` no es cosmético: apaga los
   overrides `.mobile` de ProjectSettings, y ahí `rendering/quality/depth/hdr` pasa de `false` a `true`,
   o sea otro render target (RGBA16F). Medido en `CoverScene` (Odisea) contra el build ES con la misma
   pose congelada: **mean 8.78 / 13479 px>30**, malla del Pilot 9.140 / 1794 px. Con la Fase 2 revertida
   la comparación queda en el piso de ruido del harness (ES vs ES: 0.035 / 190 px>30; ES vs GL:
   0.037 / 221 px>30; malla: 0.025 / 46 px). O sea: **la "malla fragmentada" y el "cielo/textura
   barridos" eran configuración, no driver** — y ahí se gastó una tanda larga de caza de fantasma.
   Detalle y verificación repetible: sección Fase 2. El harness además fija los 13 valores del device en
   el proyecto de captura (`test_project/override_mobile_parity.cfg`, `PARITY_OVERRIDE=0` lo apaga) por
   si el reporte de features vuelve a divergir.

7. **La "malla fragmentada" era el parche de decal desbordando el presupuesto de condicionales del
   scene shader.** No era el camino Desktop-GL, ni el prepass per se, ni la configuración: era
   `zzz_feature_decal_gles3.patch`. El loop de decal en `scene.glsl` usaba `#ifdef ENABLE_AO` donde
   upstream escribe `#if defined(ENABLE_AO)`, y `gles_builders.py` convierte **todo** `#ifdef` en un
   condicional del shader (`re.sub(r".*#ifdef (\S+).*")`, `gles_builders.py:72-87`). Eso dejaba **32**
   condicionales y empujaba `SHADELESS` al **bit 31**, que es `VersionKey::UBERSHADER_FLAG`
   (`shader_gles3.h:150`): el estado de variantes y el flag de ubershader se pisaban, `_bind()`
   cortaba el rebind con el early-return "ubershader → ubershader" y la pasada de profundidad se
   quedaba con el programa anterior, **sin skinning**. El prepass escribía entonces la profundidad de
   la malla en pose de bind, y la pasada de color —ya skinneada— se auto-ocluye contra ella: por eso
   el defecto **solo aparece animando** (en reposo bind pose y skinning coinciden) y por eso lo
   escondían tanto `FRT_NO_DEPTH_PREPASS` como `FRT_SKIN_NO_DEPTH`. Afectaba a **todos** los builds
   (x11, FRT/ES y por lo tanto también el target arm64), no solo al desktop.

   Medición (CoverScene dentro del preview del menú de Odisea, pose congelada
   `CAPTURE_ANIMATION_TIME=0.6`, mismo `CAPTURE_SKELETON_MD5` en todos los casos, contra el x11
   `opt.tools.64s` de referencia sin el parche):

   | build x11 | diff vs referencia |
   |---|---|
   | con el parche de decal (roto) | mean 8.044 / 2004 px>30 |
   | sin el parche de decal | mean 0.144 / 17 px>30 |
   | con el parche + `#if defined(ENABLE_AO)` | **mean 0.059 / 7 px>30** |

   Consecuencia para lo anterior: la malla de esas capturas estaba rota **en los dos bins** que se
   comparaban (ES y desktop-GL), así que los números de "paridad" del hallazgo 6 midieron dos renders
   igual de rotos. La conclusión de `depth/hdr` sigue en pie como causa de la diferencia ES↔desktop-GL,
   pero conviene re-verificarla con el fix aplicado. El scene shader queda **al límite**: 31
   condicionales es el máximo, porque el bit 31 es de `UBERSHADER_FLAG`; el próximo que se agregue
   necesita mudar ese flag o recortar condicionales.

---

## Dónde viven los cambios

`godot-box3d-3` es el repo de módulo + parches. El árbol del motor (`../godot`) es **generado**:
`scripts/build.sh:103-106` aplica `patches/*.patch` sobre Godot, y `frte_prep()` (`build.sh:139-157`)
clona `platform/frt` desde efornara/frt en `FRT_REF` y le aplica `patches/frt/*.patch`.

Todo lo de abajo se entrega como archivos de parche. Nada se edita in situ de forma permanente.

---

# Fase 1 — Desktop-GL profile (la prioridad)

Estrategia: **reusar glad exactamente como x11**, no regenerar el wrapper `dl/`. Razón dura, no estética:
`drivers/gles3/rasterizer_storage_gles3.cpp:8249-8287` usa `GLAD_GL_ARB_get_program_binary`,
`GLAD_GL_ARB_parallel_shader_compile` y `glMaxShaderCompilerThreadsARB/KHR` dentro de `#ifdef GLES_OVER_GL`.
Esos símbolos **sólo existen si el loader es glad**. Sin glad habría que parchear cada sitio `GLAD_*`
o perder el shader cache por program-binary — que es justo lo que sostiene
`gles3_shader_cache_decoupled.patch` y `gles3_async_compilation_runtime_toggle.patch`.

### 1.1 La opción

`platform/frt/detect.py` → `get_opts()`: `BoolVariable('frt_desktop_gl', ..., False)`.
SCons deja las variables de opción en el `env` compartido, y `drivers/SCsub` se evalúa después de
`opts.Update(env)` y de `detect.configure(env)`, así que `env["frt_desktop_gl"]` es legible desde
`drivers/gl_context/SCsub`. Es el mismo mecanismo que usa x11 hoy.

En `configure_misc()`, bajo la opción: `-DOPENGL_ENABLED`. Sin él,
`rasterizer_gles3.cpp:146` aplica el check de ES (`major >= 3`) en vez del de desktop (`>= 3.3`), y un
contexto 3.1 pasaría el filtro para morir después con un muro de errores GLSL en el primer `#version 330`.
Verificado inerte fuera de `platform/{windows,x11}`: sus únicos consumidores son
`rasterizer_gles3.cpp:146` y `rasterizer_gles2.cpp:176`.

### 1.2 Archivos y hunks

**Nuevo `patches/frt_desktop_gl.patch`** (árbol del motor):

- `drivers/gl_context/SCsub:7` — sumar la condición:
  ```python
  if env["platform"] in ["haiku", "osx", "windows", "x11"] or env.get("frt_desktop_gl", False):
  ```
  `env.get` es seguro en las otras plataformas, donde la opción no está registrada.

- `drivers/gles3/rasterizer_gles3.cpp` y `drivers/gles2/rasterizer_gles2.cpp` — el loader correcto,
  con la misma forma que el precedente ya existente en `patches/frt_platform_hooks.patch`
  (que hace `#define eglGetProcAddress(x) SDL_GL_GetProcAddress(x)`):
  ```c
  #if defined(FRT_ENABLED) && defined(GLAD_ENABLED)
  extern "C" void *SDL_GL_GetProcAddress(const char *);   // a nivel de namespace, no dentro de is_viable()
  #define frt_glad_load() gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)
  #else
  #define frt_glad_load() gladLoadGL()
  #endif
  ```
  y en `is_viable()`: `gladLoadGL()` → `frt_glad_load()`.
  **Los dos drivers**, no sólo GLES3: GLES2 es el fallback en runtime y dejarlo a medias cambia un
  error de compilación por un crash de puntero null.

  *Alternativa de ordenamiento:* `patches/*.patch` se aplica por orden de glob, y
  `frt_platform_hooks.patch` ya es dueño de `rasterizer_gles2.cpp`. Si preferís evitar el offset,
  plegar estos dos hunks ahí adentro en vez de crear el parche nuevo.

**Nuevo `patches/frt/desktop_gl.patch`** (clone de FRT):

- `detect.py` — la opción y `-DOPENGL_ENABLED` (§1.1).
- `platform_config.h` — espejo de `platform/x11/platform_config.h`:
  ```c
  #ifdef GLAD_ENABLED
  #define GLES2_INCLUDE_H "thirdparty/glad/glad/glad.h"
  #define GLES3_INCLUDE_H "thirdparty/glad/glad/glad.h"
  #else
  #define GLES2_INCLUDE_H "dl/gles2.gen.h"
  #define GLES3_INCLUDE_H "dl/gles3.gen.h"
  #endif
  ```
- `frt_godot.cc:98-110` `init_video()` — **el hunk crítico** (hallazgo 4): bajo `GLAD_ENABLED`,
  llamar `RasterizerGLES{2,3}::is_viable()` donde antes iba `frt_resolve_symbols_gles{2,3}()`.
  `is_viable()` carga glad *y* hace el check de versión *y* devuelve un `Error` reportable.
  Fallar con el `fatal()` que ya usa `sdl2_adapter.h::init_window()`.
  Agregar además una línea de `print_verbose` con `glGetString(GL_VERSION)` — compila en ambas
  configuraciones (macro de glad o macro del wrapper `dl/`) y es el gancho de verificación de §1.4.
- `sdl2_adapter.h:427-444` `init_window()` — bajo `#ifdef GLES_OVER_GL`, pedir **lo mismo que x11**
  (`platform/x11/context_gl_x11.cpp:186-193`): 3.3, `SDL_GL_CONTEXT_PROFILE_CORE`,
  `SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG`. GLES2-over-GL: 2.1 sin profile mask. El `#else` conserva
  el bloque ES actual **byte por byte**.
- `sdl2_adapter.h:423-426` `init_context_gl()` — check de null en `SDL_GL_CreateContext` con
  `SDL_GetError()`. Es un bug real hoy (`SDL_GL_MakeCurrent(window_, NULL)` da un null-deref varios
  frames después, sin mensaje), y bajo el perfil nuevo es el modo de falla *esperado*.

**`scripts/build.sh:205`** — `frt_desktop_gl=yes` **sólo** en la línea de `frt-editor`.
`frt-arm64-templates` (`:206-224`) no se toca.

### 1.3 Decisiones cerradas, con su razón

- **Compile-time, no runtime.** `GLES_OVER_GL` decide en tiempo de compilación qué `#version` se emite
  y qué tabla de internal formats existe. Un toggle en runtime exigiría las dos variantes de shader y
  los dos caminos de storage en un binario — la abstracción que no queremos. Los dos binarios ya
  difieren (`frt-editor` vs `frt-arm64-templates`), así que no cuesta nada.
- **CORE, no COMPATIBILITY**, pese a que el glad en árbol es un loader de perfil *compatibility*
  (`thirdparty/glad/glad/glad.h`, glad 0.1.34). Un header compat sólo declara más entry points; los de
  más quedan en null porque Godot no los llama. x11 lleva años con CORE+forward-compat contra este
  mismo glad: esa es la evidencia de que el driver GLES3 de 3.6 no usa nada removido en core
  (verificado: todo draw bindea un VAO real, no hay `GL_QUADS` ni client-side arrays; `GL_LINE_SMOOTH`
  en `rasterizer_canvas_gles3.cpp:527+` sigue siendo core en 3.3). Y el perfil compat de Mesa es el
  camino *menos* confiable — elegirlo reintroduciría la misma clase de problema que estamos evitando.
- **Sin `GraphicsAPI` nuevo.** `API_OpenGL_ES2/ES3` siguen significando "el driver GLES2" y "el driver
  GLES3", que sigue siendo cierto.
- **Sin fallback-a-ES.** Bajo `GLES_OVER_GL` el binario sólo contiene shaders `#version 330`.
  Reintentar con un contexto ES da una ventana que no compila ni un shader. Fallar fuerte.
- **`platform/frt/SCsub` no se toca.** `dl/gles{2,3}.gen.cc` siguen compilando (incluyen
  `<GLES2/gl2.h>`/`<GLES3/gl3.h>`, que definen `__gl2_h_`/`__gl3_h_` y no `__gl_h_`, así que el
  `#error` de glad no dispara). Quedan ~250 globals sin usar. Sacarlos es un hunk más por cero beneficio.

### 1.4 Verificación

```
scripts/build.sh frt-editor
godot/bin/godot.frt.opt.tools.x86_64 --verbose --path test_project 2>&1 \
  | grep -i 'GL_VERSION\|Renderer\|video driver'
```
La prueba es la **ausencia de la subcadena `OpenGL ES`** en `GL_VERSION`; se espera
`3.3 (Core Profile) Mesa …`. Ojo: `rasterizer_gles3.cpp:188` imprime siempre el literal
`OpenGL ES 3.0 Renderer: <GL_RENDERER>` — la etiqueta miente, es sólo `glGetString(GL_RENDERER)`.

Segunda prueba, gratis: los shaders están atados al perfil en tiempo de compilación. `#version 330` no
compila en un contexto ES y `#version 300 es` no compila en uno core. Si se dibuja algo, contexto y
shaders coinciden.

Orden de corrida: (1) `test_project` en el editor; (2) `--no-window` (feature de FRT vía
`patches/frt/sdl_hidden_window.patch`, buen smoke test headless para CI); (3) `test_project/demo` y
`test_project/bench` — física Box3D bajo carga, es donde se notaría el patch de sampler budget o el de
batching portándose mal; (4) **Odisea al final** — es el test de regresión real de
`feature_decal_gles3.patch`, el único proyecto que usa decals; (5) el crash original bajo
`SDL_VIDEODRIVER=wayland` **y** `SDL_VIDEODRIVER=x11` — el caso Wayland/EGL es precisamente para lo que
existe el parche de `gladLoadGLLoader`.

Probar que arm64 no se movió, sin hardware:
- `scripts/build.sh frt-arm64-templates` aplica todos los parches limpio.
- `scons --dry-run` de ambos targets: la línea de arm64 no debe contener `-DGLAD_ENABLED`,
  `-DGLES_OVER_GL` ni `-DOPENGL_ENABLED`, y sí debe seguir con `-DGLES_ENABLED -DFRT_ENABLED`.
- Todo hunk de FRT está dentro de `#ifdef GLAD_ENABLED`/`#ifdef GLES_OVER_GL` salvo el check de null de
  `SDL_GL_CreateContext`, que es una mejora compartida intencional.

CI: no agregar nada. `.github/workflows/release.yml:44-51` ya construye los dos targets.

### 1.5 Hitos

- **M0 — compila.** `scripts/build.sh frt-editor` con la opción. No corre nada todavía. Saca a la luz
  colisiones de headers de glad, las declaraciones de `frt_resolve_symbols_*`, el bloque
  `#if !defined(GLES_OVER_GL)` de GLES2 y cualquier `GLAD_GL_*` que no resuelva. Es el hito más chico
  posible y drena casi todo el riesgo de integración.
- **M1 — contexto 3.3 y ventana.** Criterio de éxito único: `GL_VERSION` sin `OpenGL ES` e `is_viable()`
  devolviendo OK. Renderizado negro o basura **está bien** en este punto. **Este hito es el que responde
  la pregunta real** (¿deja de crashear gallium?) y se alcanza antes de depurar un solo shader.
- **M2 — el editor renderiza.** Acá vive el trabajo de GLSL 330 (ver riesgo 2).
- **M3 — arm64 intacto**, según §1.4. Barato, hacerlo antes de mergear.

### 1.6 Interacción con el stack de parches existente

- `gles3_ubershader_sampler_budget.patch` — **se relaja solo** en desktop: cada `texunit:-N` se calcula
  contra `config.max_texture_image_units` (`rasterizer_storage_gles3.cpp:8205`,
  `shader_gles3.cpp:1244`), que en Mesa es 32 contra los 16 de Adreno. No requiere cambios. **Pero** su
  flag `RADIANCE_MAP_ARRAY_AVAILABLE` cuelga de `rendering/quality/reflections/texture_array_reflections`,
  que tiene override `.mobile = false` → ver fase 2.
- `feature_decal_gles3.patch` — **el único riesgo GLSL real**. 2591 líneas que nunca se compilaron como
  GLSL de escritorio. `shader_gles3.cpp:683,750` deja de emitir `precision highp sampler2D/samplerCube/
  sampler2DArray` (ilegal en desktop GLSL; `precision highp float/int` sí son legales desde GLSL 1.30).
  El `scene.glsl` de upstream ya compila como 330 en toda build de x11, así que la exposición nueva es
  sólo lo que el parche agrega.
- `gles3_async_compilation_runtime_toggle.patch` — **no** asume ES; su hunk cae justo arriba del
  `#ifdef GLES_OVER_GL`/`#else` preexistente y ambas ramas consumen el valor. En desktop enruta por
  `glMaxShaderCompilerThreadsARB/KHR`, que resuelven porque glad se generó con esas extensiones.
- `gles3_shader_cache_decoupled.patch` — funciona; `program_binary_supported = GLAD_GL_ARB_get_program_binary`.
  Sin riesgo de cache viejo: `shader_gles3.cpp:832-838` hashea `GL_VENDOR`+`GL_RENDERER`+`GL_VERSION` en
  la clave, así que las corridas ES y desktop caen en entradas distintas solas.
- `gles3_real_variant_batching`, `gles3_storage_thread_safety`, `scene_glsl_directional_ubo`,
  `sky_thread_safety` — sin `GLES_OVER_GL`, sin `#version`, sin tokens de ES. Inertes.

### 1.7 Riesgos nombrados

1. **Olvidar que FRT nunca llama `is_viable()`.** Da segfault en la primera llamada GL sin diagnóstico,
   idéntico en apariencia al crash de gallium que se está persiguiendo. Es lo más caro de esta página.
2. **`#version 330` contra `feature_decal_gles3.patch`.** El único lugar donde espero trabajo de arreglo
   de verdad. Si M2 se atasca, aislarlo sacando el parche de decals temporalmente.
3. **`gladLoadGL()` "funciona" en esta máquina.** libglvnd tapa el problema local y reaparece en otra
   distro o en un contenedor. Tomar el parche de `gladLoadGLLoader` igual.
4. **`SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG`** — riesgo bajo (x11 lo usa), pero es la perilla más barata
   si M2 produce `GL_INVALID_OPERATION` inexplicables: sacar el flag primero; CORE→COMPATIBILITY sólo
   como último recurso y con el caveat de Mesa en mente.
5. **Colisión de nombre de artefacto.** Los binarios ES y GL comparten `godot.frt.opt.tools.x86_64`.
   SCons reconstruye bien (los CPPDEFINES entran en la firma), pero un humano comparando dos binarios en
   `bin/` se confunde. Mitigación: leer siempre la línea `GL_VERSION`, no confiar en el nombre.
6. **El crash puede no ser el perfil ES.** Toda la premisa es una atribución. **M1 la responde barato y
   temprano**: si un contexto 3.3 de escritorio crashea igual, parar y ir a depurar el crash real en vez
   de terminar M2/M3.

---

# Fase 2 — `has_feature("mobile")` en desktop: **REVERTIDA**

Se implementó (envolver en `#ifdef GLES_OVER_GL` el `return feature == "mobile" || feature == "etc";`
y devolver las features de escritorio) y se revirtió al medir el efecto. La decisión original era "el
frt-editor debe comportarse como escritorio"; la medición la dio vuelta.

El análisis que la justificaba sigue valiendo y es útil: **ningún C++ lee
`has_feature("mobile"/"etc"/"etc2"/"s3tc")`** — el único call site de `has_feature("` en
`core scene servers main editor drivers modules` es `"primary_clipboard"`. El formato de textura lo
decide `GLES_OVER_GL` directamente en `rasterizer_storage_gles3.cpp:8213-8224`
(`etc2_supported=false; s3tc_supported=true; rgtc_supported=true`), sin consultar al OS. El efecto
**único** son los overrides `.mobile` de ProjectSettings (`core/project_settings.cpp:200-211`), 13
settings: `directional_shadow/size` y `shadow_atlas/size` 2048, `shadows/filter_mode` 0,
`reflections/texture_array_reflections` false, `reflections/high_quality_ggx` false,
`shading/force_vertex_shading` true, `force_lambert_over_burley` true, `force_blinn_over_ggx` true,
`depth/hdr` false, `intended_usage/framebuffer_allocation` 3, y los tres de `gles3/shaders/`.

Por qué se revirtió: entre esos 13 está `rendering/quality/depth/hdr`, así que el build desktop-GL
pasaba a dibujar en un render target HDR (RGBA16F). En `CoverScene` (Odisea), misma pose congelada,
contra el build ES: **mean 8.783 / 13479 px>30**, con la malla del Pilot aportando 9.140 / 1794 px.
Igualar sólo `depth/hdr` no alcanza: baja a 0.894 / 4895, porque el resto de los overrides sigue
divergiendo.

Estado tras revertir: el build desktop-GL resuelve los mismos valores que el ES (verificado con el
probe: `mobile=True`, `depth/hdr=false`, `filter_mode=1`, `directional_shadow/size=2048`,
`max_simultaneous_compiles=1`) y la comparación queda **en el piso de ruido del harness** — mismo
binario dos veces: 0.035 / 190 px>30; ES vs desktop-GL: 0.037 / 221 px>30. La malla del Pilot coincide
salvo 46 px (por debajo del piso de ruido: 127 px). O sea: la "malla fragmentada" y el "cielo/textura
barridos" que se venían persiguiendo eran configuración, no driver.

Verificación (repetible): `CAPTURE_ANIMATION_TIME=1.0 scripts/compare_glitch.sh res://scenes/CoverScene.tscn 180 <out>`
con `CAPTURE_PROJECT` apuntando a Odisea; el piso de ruido se mide con `GODOT_GL=<binario ES>`.
`test_project/override_mobile_parity.cfg` queda como cinturón: fija los 13 valores del device en el
proyecto de captura mientras dura la corrida, por si el reporte de features vuelve a divergir.

---

# Fase 3 — Clipboard

Lo más barato de toda la lista, y hoy **copiar/pegar entre el frt-editor y cualquier otra app está muerto**:
FRT no sobrescribe nada y cae al default de `core/os/os.cpp:162-179`, que es un `String _local_clipboard`
en proceso.

- `sdl2_adapter.h` — dos wrappers: `SDL_SetClipboardText` / `SDL_GetClipboardText` (el segundo devuelve
  memoria que hay que `SDL_free`), más `SDL_HasClipboardText`.
- `frt_godot.cc` — overrides de `set_clipboard` / `get_clipboard` / `has_clipboard`, al lado de los stubs
  de cursor en `:313-316`.

**No implementar primary selection.** `SDL_GetPrimarySelectionText` es SDL ≥ 2.26 y el toolchain arm64
está pineado a SDL2 2.32.10 — alcanzaría, pero el default de `OS::` (`_primary_clipboard`) ya no rompe
nada y es una superficie menos. Agregar si alguien se queja del pegado con botón central.

Verificación: copiar del editor y pegar en otra app, y al revés. Si funciona en las dos direcciones, está.

---

# Fase 4 — IME (paridad con x11, alcance mínimo)

Decisión tomada: paridad con x11, sin preedit.

- `set_ime_active(bool)` → `SDL_StartTextInput()` / `SDL_StopTextInput()`.
- `set_ime_position(Point2)` → `SDL_SetTextInputRect(&rect)`.

Es exactamente lo que hace x11 (`os_x11.cpp:852-893`: `XSetICFocus`/`XUnsetICFocus` y `XNSpotLocation`).
x11 **no** implementa `get_ime_text` / `get_ime_selection` ni emite `NOTIFICATION_OS_IME_UPDATE` — eso sólo
lo hacen osx y javascript. El texto ya confirmado por el IME llega hoy por `SDL_TEXTINPUT`, que FRT ya
maneja (`sdl2_adapter.h:491`).

~15 líneas. No se agrega manejo de `SDL_TEXTEDITING`.

Verificación: con un IME activo (ibus/fcitx), escribir en un campo del editor y confirmar que el texto
commiteado llega y que la ventana de candidatos se posiciona sobre el cursor y no en una esquina.

---

# Cola (fuera de alcance de este plan)

Explícitamente despriorizados, con lo que ya se sabe para cuando toque:

**Cursores.** `frt_godot.cc:313-316` son stubs vacíos y `get_cursor_shape()` reporta `CURSOR_ARROW` para
siempre. Godot tiene 17 formas (`core/os/os.h:468-486`), SDL2 tiene 12 cursores de sistema → hay que
aliasar 5. x11 ya define el aliasing bueno (`os_x11.cpp:618-636`): DRAG→`fleur`, CAN_DROP→`hand1`,
VSPLIT→`sb_v_double_arrow`, HSPLIT→`sb_h_double_arrow`. Custom: `SDL_CreateRGBSurfaceFrom` +
`SDL_CreateColorCursor`; vale la pena copiar de `os_x11.cpp:3780-3879` el `cursors_cache`, el unwrap de
`AtlasTexture` y las validaciones (rechazar > 256px, chequear el hotspot).

**Drag & drop.** No existe `NOTIFICATION_WM_DROP_FILES` en Godot 3.6 — el mecanismo es la llamada directa
`main_loop->drop_files(files, 0)` (`core/os/main_loop.h:76`, override de SceneTree en
`scene/main/scene_tree.cpp:1932-1934`). El modelo a copiar es Windows (`os_windows.cpp:1179-1196`), no la
máquina de estados XDND de x11. En SDL: un `case SDL_DROPFILE` en el switch de `sdl2_adapter.h:491`,
recordando que SDL entrega **un archivo por evento** (hay que batchear entre `SDL_DROPBEGIN` y
`SDL_DROPCOMPLETE`) y que `event.drop.file` se libera con `SDL_free`.

---

## Archivos críticos

| Archivo | Qué |
|---|---|
| `../godot/platform/frt/frt_godot.cc` | `init_video()` :98-110 (el swap a `is_viable()`), `_check_internal_feature_support()` :166-172, stubs :313-316 |
| `../godot/platform/frt/sdl2_adapter.h` | `init_context_gl()`/`init_window()` :423-444, switch de eventos :491 |
| `../godot/platform/frt/detect.py` | `get_opts()` :26-32, `configure_misc()` :68-77 |
| `../godot/platform/frt/platform_config.h` | :10-11, el redirect de headers |
| `../godot/drivers/gl_context/SCsub` | :7, la lista de plataformas que enciende glad + `GLES_OVER_GL` |
| `../godot/drivers/gles3/rasterizer_gles3.cpp` | `is_viable()` :138-156; misma forma en `gles2/rasterizer_gles2.cpp:170` |
| `scripts/build.sh` | `frt-editor` :196-205, `frte_prep()` :139-157 |
| `patches/README.md` | documentar la opción nueva y los dos ítems que resultaron ser no-trabajo |

## Orden de ejecución

1. Fase 1 (M0 → M1 → M2 → M3) — **parar en M1 y confirmar que el crash desapareció antes de seguir.**
2. Fase 2 — **revertida**: se midió y el desktop-GL tiene que renderizar como el device (ver Fase 2).
3. Fase 3 (clipboard) — independiente de todo lo anterior, se puede adelantar si la fase 1 se atasca.
4. Fase 4 (IME).
