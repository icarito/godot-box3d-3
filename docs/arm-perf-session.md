# Sesión ARM/perf (2026-09-25): qué hicimos y qué aprendimos

Cierre de la investigación de rendimiento en el handheld RK3326 y de la evaluación de
`lawnjelly/godot-titan` para el fork. Documentos de detalle: `docs/arm-gdscript-hotspots.md`
(puntos calientes de GDScript) y `docs/local-vs-ci-build.md` (local vs CI, pck/replay).

## 0. Resumen ejecutivo

- **Ganancia adoptada:** `lto=full` en los templates arm64 → **+5.1% fps** y **−13% de
  binario**, medido con A/B intercalado y pareado (n=16, 15/16 positivo, p<0.001) en
  `angel`. Ya aplicado en `scripts/build.sh` (`LTO=full` en `linux-arm64-templates` y
  `frt-arm64-templates`).
- **Negativos útiles (no insistir):** `-mtune=cortex-a35` no ayuda; los binarios
  "optimizados" del bench (`mcpu`, `newpin`, `gputimer`, `nightly13/14`) no superan al
  baseline más allá del ruido; `LPortal`/`LLightmap`/`ECMAScript` de godot-titan no
  aportan a un fork 3.6 (detalle en §5).
- **GDScript:** quick wins implementados en Odisea (~0.6–1.2 ms/tick sobre un tick de
  ~12.5 ms). Ver §4.
- **Lección de método:** el device es tan ruidoso que **solo el A/B pareado e intercalado**
  resuelve efectos <10%. Comparaciones no pareadas de fps en este device no valen.
- **Lección de contrato replay:** un replay solo es válido contra el build con el que se
  grabó. El replay pineado dejó de reproducir al cambiar el contenido el 24-sep (§6).

## 1. Entorno y carga de referencia

- Device `angel`: ROCKNIX, aarch64, RK3326 (4× Cortex-A35 **in-order**), ~981 MB RAM,
  Mali-G31, GLES3. Governor por defecto `ondemand` (se fija a `performance` para medir).
- Carga: `replay_1790167671.json` (RingHub_Level) con `ODISEA_REPLAY_PERF=1` +
  `--replay user://…`; salida `user://replay_perf.json` (`frames`, `perfiles[]`, `muestras[]`).
- Contexto **primario** (válido): `odisea.pck.prev` = build local de `7cea9461`,
  replay-consistente (`DRIFT_CHECK 0.0003`), frames=1352, baseline ≈ **10.46 fps**,
  `ms_process` p50 ≈ 37, `ms_physics` ≈ 19.
- Contexto **secundario** (inválido, ver §6): `odisea.pck` = build local de `cd60d7ca`,
  drift 18.07.

## 2. Flags de compilación arm64

Estado real del binario shipeado (capturado de un compile/link real, no inferido):

| ítem | valor |
|---|---|
| Optimización | `-O3 -ffast-math -fomit-frame-pointer -fno-exceptions` |
| **LTO** | **OFF** (`build.sh` fijaba `lto=none`) |
| `-march/-mcpu/-mtune` | ninguno; el default del SDK es `-march=armv8-a+crc -mcpu=cortex-a53` (¡no A35!) |
| Strip | sí (`-s`, `production=yes`, sin `.symtab`) |
| Box3D | aarch64 → `B3_SIMD_NEON` activo; `-ffp-contract=off` preservado |

A/B pareado en `angel` (intercalado, `governor=performance`, n=16):

| variante | Δfps vs base | reps | veredicto |
|---|---|---|---|
| `lto=full` | **+0.53 (+5.1%)** | 15/16, t=4.42 (p<0.001) | adoptada |
| `-mtune=cortex-a35` | −0.27 | 2/8 | sin efecto |
| control `base` vs baseline | +0.17 | dentro del SD (0.51) | control válido |

No-ops en este target: `-fno-semantic-interposition` (exe no-PIE), `-moutline-atomics`
(A35 sin LSE), `-falign-*` (bloat, sin ganancia). PGO/BOLT: teórico, cross-compile caro.

## 3. Ruido y método (lo que más vale para futuras mediciones)

- Piso de ruido **no pareado**: spread de fps 1.28 (n=10) y `ms_process` medio ±8–23 ms.
  Con eso, un efecto del 5% es indistinguible.
- Piso **pareado** (SD del control base-vs-baseline): fps 0.51. El efecto LTO (0.53) es
  ~4× el SEM a n=16.
- Regla: **round-robin intercalado con offset rotado** por sesión, governor fijo,
  temperatura anotada, y veredicto contra el piso pareado. Nada de comparar rangos no
  pareados para efectos <10%.

## 4. GDScript en ARM

- El intérprete de GDScript 3.x no se vectoriza: ni SSE ni NEON cambian nada del script.
  El SIMD solo importa en C++, donde Box3D ya usa NEON en arm64 (y cae a escalar en ARMv7).
- Las ganancias reales en ARM vienen de: **aarch64** (> armv7), **tipado estático**,
  sacar trabajo del intérprete (nativos/shaders), y **térmica/tasa de tick**
  (`physics/common/physics_fps`, cap de fps).
- Quick wins implementados (commit `cb3b7429` en Odisea):
  1. `SessionManager`: sub-lista `_replay_sync_step_cache` (sin `has_method("step")` por
     nodo y tick).
  2. `SessionManager`: cache de `SceneManager`/`VideoExporter` (autoloads) en vez de
     `get_node_or_null` por tick.
  3. `SessionManager`: `record-toggle` solo se sondea en export/debug/editor.
  4. `InteractableBaseV2`: `_perf_detailed` cacheado (sin 2 `has_method` por `step()`).
  5. `KinematicArm3D`: reuso de un `PhysicsShapeQueryParameters`.
  Ahorro estimado **~0.6–1.2 ms/tick**; determinismo verificado (los 4 replays OYS dan
  `dist=0.000000`).
- Refutado contra el engine: el swap de `collision_mask` de `_try_step_up` **no** cuesta
  bajo Box3D (`Box3DBody::apply_filter` ignora `maskBits`; `b3Shape_SetFilter` retorna
  temprano). El costo son los barridos de cápsula.
- Pendiente de A/B: §2.2 (`_try_step_up`), §2.6 (unificar barridos de slide), §2.7
  (tipado), §2.9 (`max_slides`/snap). Tocan trayectoria → requieren re-grabar replays.

## 5. godot-titan: veredicto

Repo de releases, no de código. Contenido: rama `titan` de Godot ~3.3-dev + módulos
LLightmap, LPortal, LSimd, ECMAScript.

- **LPortal**: superado; es lo que se subió a core como Rooms & Portals en 3.4 (ya en 3.6).
- **LLightmap**: solapa con el lightmapper CPU de core; su diferencial real es el **atlas
  de lightmap único compartido** (menos texturas/cambios de estado). Port completo caro y
  riesgoso; a lo sumo, robar la idea.
- **LSimd**: único rendimiento no duplicado, pero es para GDScript (`FastArray_*`) y sus
  intrinsics son **solo SSE** (en FRT no se compilan; en arm64 cae a loop autovectorizable).
  No aplica a Box3D (que ya tiene su SIMD). Valor niche.
- **ECMAScript**: irrelevante.

## 6. El episodio pck/replay (importante para el pipeline)

- Los dos pck del device eran builds **locales**, no de CI: `odisea.pck` = `cd60d7ca`
  (24-sep), `odisea.pck.prev` = `7cea9461` (23-sep). El release de CI no estaba en el device.
- Entre esos commits cambió RingHub entero (pisos/meshes, `BakedLightmap`, luz, botón,
  `RingHubLightState.gd`). Por eso el replay pineado **no reproduce** en `cd60d7ca`
  (`DRIFT 18.07`): el recorrido grabado ya no coincide con la geometría y el jugador
  **atraviesa el piso**. No es una regresión de Box3D; en `pck.prev` el mismo replay
  reproduce con `DRIFT 0.0003` y la física coincide.
- **El "2× más pesado" del build local no era real:** `ms_process` mediano 37.0 (`prev`)
  vs 37.3 (`actual`); fps 10.47 vs 10.26; GDScript tick 9.38 vs 9.42. El 2× salía de un
  plateau de ~1.9× (331 vs 641 ms/frame) en **regiones distintas** por el drift.
- **Defecto real:** el pck `cd60d7ca` trae `RingHub.lmbake` de **332 B con octree vacío**
  (placeholder pre-fix); el fix `a0d26c44` lo deja en 3569 B (igual que CI). Causa los
  errores `lightmap_capture_set_octree`/`_set_user_data` y el SIGSEGV intermitente.
- **Brecha de packaging:** el pck local pesa 316 MB vs 208 MB del CI: ~112 MB de `.stex`
  ETC + S3TC que el Mali-G31 nunca muestrea. CI limita formatos por plataforma
  (`export_platform.yml`, "Limit texture import formats"); el pipeline local no.
- Reglas que quedan: (1) un replay debe re-grabarse al cambiar contenido que afecte
  trayectoria; (2) benchmarkear solo contra un workload que reproduzca (DRIFT ≈ 0);
  (3) adoptar el limitado de formatos de textura en el pipeline local.

## 7. Higiene de build del fork

Al intercalar en el mismo árbol un build `tools=yes` (editor x11) y luego `tools=no`, queda
stale el generado `modules/modules_enabled.gen.h` y el build rompe con
`register_cvtt_types was not declared`. Workaround: borrar los generados
`modules/modules_enabled.gen.h` y `modules/register_module_types.gen.cpp` antes de rebuild.
No afecta a un checkout limpio; sí a la alternancia de variantes en un mismo árbol.

## 8. Estado y pendientes

Hecho:
- `scripts/build.sh`: `lto` parametrizado y `LTO=full` en los templates arm64.
- Odisea: quick wins de GDScript commiteados (`cb3b7429`), determinismo verificado.
- Stats del device archivadas en `scripts/odisea_bench_results/` (harness + resultados).
- Device reseteado al último release publicado (`nightly` = `0.5.0-nightly.744+6755669`),
  pck CI (208 MB) + engine; ver `scripts/odisea_bench_results/reset_manifest.md`.

Pendiente (propuesto, con A/B en device):
1. Confirmar LTO en un pck **actual y válido** (≥ `a0d26c44`), no en el pck roto.
2. Re-grabar `replay_1790167671` contra el build del release y retirar el viejo.
3. Adoptar el limitado de formatos de textura de CI en el pipeline local (−112 MB).
4. Medir los quick wins GDScript (requiere rebuild de pck + A/B).
5. Evaluar `-mcpu=cortex-a35`/afinado por SoC: sin ganancia medida; no priorizar.
