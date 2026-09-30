# Regresion de rendimiento "nightly3 -> nightly5" (FRT arm64, Anbernic)

Fecha: 2026-09-30. Rama de trabajo: `fix/frt-video-driver-gles3`.
Medido con el playback reproducible del render-esclavo (RingHub, `snapshot_playback`,
`script_ms` = tramo de `_process` de todo el arbol que mide `PerfilTickSentinela`).

## TL;DR

**No hay regresion entre v0.5.4-nightly3 y v0.5.4-nightly5.** El release de CI de
nightly3 trae el modulo ImGui y mide igual que nightly5. Los ~14.6 ms de
`script_ms` que se tomaron como "nightly3" salen de un binario local distinto
(`md5 acf0ffd1...`, 35.4 MB, version `6371881f6`) que **NO trae el modulo ImGui**.

El unico delta de fuente del motor entre nightly3 y nightly5 es el modulo `slug`
(inerto: el juego no instancia `SlugLabel3D`). La diferencia de ~6 ms la explica
la **presencia del modulo `imgui`**, que habilita el camino de UI ImGui del juego
(`ClassDB.class_exists("ImGuiCanvas")` en `CryoPodUI`, `FlashlightScreen`,
`ProtocolScreen`, `DebugHud*`, etc.). Con el modulo presente: `script_ms` ~20.5;
sin el: ~15.

## 1. Que compila cada build (evidencia a nivel binario)

| build | modulo ImGui | modulo slug | LTO | md5 | tamano |
|---|---|---|---|---|---|
| `v0.5.4-nightly3` (asset CI) | **si** | no | si | `875369b8bd3d5b04e46d40c009e0dd3c` | 31.816.488 |
| `v0.5.4-nightly5` (asset CI) | **si** | si | si | `bc7e8ca3c3c3f320b21a635a26fe5bb2` | 31.898.408 |
| local `box3d-3.6@29369d29` ("slugfix") | **si** | si | si | `a751591638152c9a0adfb0a5ebc785e3` | 31.882.024 |
| local `bak-nightly3` (el "nightly3" del bench) | **no** | no | no | `acf0ffd13ad08222a917f9e89166fa8c` | 35.417.816 |
| `godot-dev/bin/godot.frt.opt.arm64.lto` (Sept 24) | **no** | no | si | `84a1df051921b2df3dc1b06841054ece` | 30.915.328 |

Identificacion por `strings`: `ImGuiCanvas` da 7 en los tres primeros y 0 en los
dos ultimos; `SlugLabel3D` da 10 solo en nightly5/slugfix. Los tamaños confirman
LTO: ~31.8-31.9 MB con LTO vs 35.4 MB sin LTO (el `.base`/`.a35` de la sesion de
perf tambien son 35.4 MB).

El binario que corria en el Anbernic como "nightly3" es
`/storage/roms/ports/odisea/odisea.frt.aarch64.bak-nightly3`: 35.4 MB, version
`3.6.4.rc.custom_build.6371881f6`, **sin ImGui** y sin slug. Es un build local de
la sesion de perf (el mismo `md5 acf0ffd1` que cita `docs/arm-perf-session.md` y
el commit de FD-316), no el asset de CI de nightly3.

## 2. Diff de la pila de parches nightly3 vs rama `box3d-3.6`

Procedimiento (worktree temporal, `GODOT_REF = 6371881f67`):

```
git -C ../godot worktree add --detach /tmp/kilo/n3tree 6371881f6742425cc14eaa367f18dd95955bf5e5
# aplicar patches/*.patch de v0.5.4-nightly3 (38 parches top-level), en orden alfabetico
git -C ../godot diff --stat <n3tree-write-tree> <tree de box3d-3.6@60f03082>
```

Resultado: **la unica diferencia son 13 archivos nuevos bajo `modules/slug`**
(`modules/slug/{SCsub,config.py,register_types.*,slug_font.*,slug_label_3d.*,slug_shader.h,doc_classes/*,SLUG_LICENSE.txt}`).
No hay ningun parche faltante, ninguno distinto y ninguno aplicado de otro modo.

- `patches/*.patch`: nightly3 = 38; HEAD = 42 (los 4 nuevos son `feature_slug_text`,
  `feature_slug_fallback`, `feature_slug_outline_docs`, `feature_slug_backend_capability`).
  Los 38 comunes son byte-identicos.
- `patches/frt/*.patch`: identicos; el unico nuevo es
  `zzzzzz_sdl_video_driver_enum.patch` (el fix de esta rama). No es la causa:
  nightly5 no lo tiene y aun asi mide igual, igual que el `slugfix` que si lo tiene.
- `patches/box3d/*.patch`: identicos.
- `scripts/engine_branch.sh check`: `OK, patches/ == box3d-3.6`.

### Flags de compilacion

`git diff v0.5.4-nightly3:scripts/build.sh v0.5.4-nightly5:scripts/build.sh` solo
cambia el modo de obtener el motor (rama del fork vs upstream+parches). El bloque
`frt-arm64-templates` es identico: `production=yes`, `LTO=full`,
`LINKFLAGS=-s`, `target=release`, `tools=no`, mismo `FRT_REF` y `frt_env.sh`
(SDK `godot-2023.08.x-4`, SDL2 2.32.10) pineados. No hay diferencia de flags.

### El modulo slug es inerto

`register_slug_types()` solo registra `SlugFont`/`SlugLabel3D`. No crea autoloads,
no toca `SceneTree` ni habilita ningun `_process`. En el juego `SlugLabel3D`/`slug`
solo aparece en `tools/slug_probe/` (herramienta aislada), nunca en `core_v2`.
Micro-bench del interprete GDScript sobre los headless x86_64 de nightly3 y
nightly5 (mismo pozo de ticks): ~37.6 ms vs ~37.5 ms, identicos. El perfilador
FRT (`zzzzz_frt_frame_profiler.patch`) y el gpu timer siguen detrás de `FRT_PERF`
(apagados por defecto) y son byte-identicos entre ambos tags.

## 3. A/B en el Anbernic (mismo pck, mismo `dev.sh`, governor `performance`)

`script_ms` = mediana (`p50`) del tramo de `_process` de todo el arbol; `frame_ms`
= mediana del frame. Se corrio el playback de RingHub con `--video-driver GLES3`.
Las corridas marcadas con (*) las hice en esta sesion con el binario ya presente
en el pck/pipeline del device; el resto son los JSON de `replay_perf`.

| build | script p50 | frame p50 | physics p50 | draw p50 |
|---|---|---|---|---|
| local `bak-nightly3` (sin ImGui, sin slug, sin LTO) | **14.7** | 55.2 | 8.1 | 56 |
| `arm64.lto` de `godot-dev` (sin ImGui, sin slug, **con LTO**) (*) | **15.2** | 58.3 | 8.1 | 56 |
| local `29369d29` (ImGui + slug, LTO) | **20.4** | 58.8 | 6.7 | 56 |
| `v0.5.4-nightly5` CI (ImGui + slug, LTO) | **20.9** | 59.8 | 7.6 | 56 |
| `v0.5.4-nightly3` CI, asset real (ImGui, **sin slug**, LTO) (*) | **21.6** | 64.7 | 9.0 | 56 |

Lectura:

- **nightly3 CI mide como nightly5 CI** (21.6 vs 20.9, dentro del ruido del
  device). La premisa de la tarea era falsa: el "nightly3" rapido no era el
  release de CI.
- **LTO queda descartado**: el binario LTO sin ImGui del 24-sep mide 15.2 (rapido),
  igual que el no-LTO sin ImGui (14.7). LTO no agrega los 6 ms.
- **slug queda descartado**: nightly3 CI no lo tiene y es tan lento como nightly5;
  el slugfix local lo tiene y no cambia nada frente al CI.
- Lo que separa 14.7/15.2 de 20.4-21.6 es, exactamente, la presencia del modulo
  **ImGui** en el binario.

## 4. Causa

El juego decide su UI con `ClassDB.class_exists("ImGuiCanvas")`
(`CryoPodUI`, `FlashlightScreen`, `ProtocolScreen`, `DebugHudScreen/Widget`,
`SuitOS`). Cuando el modulo ImGui esta compilado en el motor, monta paneles
`ImGuiCanvas` que rearman su frame y geometria en el hilo principal; cuando no
esta, cae al camino clasico de `_draw` a baja frecuencia. La diferencia medida es
el costo del camino ImGui (HUD/terminal), que cae en `script_ms` y no en
`render_ms` ni en draw calls (siguen en 56). Esto es contenido/UI del juego, no
una regresion del motor: los dos releases de CI (nightly3 y nightly5) incluyen
ImGui desde antes de nightly3.

## 5. Recomendaciones

1. **No tocar el motor ni `scripts/build.sh` por este tema.** No hay regresion de
   motor que arreglar; el cambio de la rama (slug) es inerto y los flags son los
   mismos. Cualquier "arreglo" en el build (p.ej. sacar ImGui del template arm64)
   cambiaria el producto, no la regresion.
2. **La accion real es del lado de Odisea (FD-316)**: el mismo trabajo que ya
   empezo con `ODISEA_LOW_UI_OPT` (no montar `ImGuiCanvas` en tier LOW, no rearmar
   el frame de ImGui por input) es el que recupera los ~6 ms. Re-medir ese A/B con
   un binario que tenga ImGui (nightly3/5 o local), nunca contra un binario sin el
   modulo.
3. Si en el futuro se quiere un template arm64 sin ImGui para handhelds, hacerlo
   como una decision de producto explicita en `scripts/build.sh` (excluir el
   modulo de `custom_modules`), midiendo que se pierde: toda la UI ImGui del juego.

## 6. Metodologia / reproducibilidad

- Pila de nightly3 vs rama: worktree `/tmp/kilo/n3tree` (patches de
  `v0.5.4-nightly3` sobre `6371881f67`) + `git diff` contra `box3d-3.6@60f03082`;
  `scripts/engine_branch.sh check`.
- Binarios: assets `godot.box3d.frt.arm64.release` de `v0.5.4-nightly3` y
  `v0.5.4-nightly5`; `godot-dev/bin/godot.frt.opt.arm64.lto`; y los del device
  (`bak-nightly3`, `slugfix`). Identificacion por `md5`/tamano/`strings`.
- Device: playback de RingHub (`Odisea.sh` con `dev.sh` de benchmark,
  `--video-driver GLES3`), governor fijado a `performance`, un reboot por corrida
  en el harness original. Los JSON crudos de la tarea estan en
  `bench_oldeng/slugfix/n5eng`; las corridas de esta sesion y los medidores p50
  se resumen en la tabla de §3.
- El device se dejo con el binario original (`odisea.frt.aarch64` =
  `29369d29`, `md5 a7515916...`) y sin los binarios de prueba extra.
