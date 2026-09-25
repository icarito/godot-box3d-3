# Odisea en ARM (RK3326, 4× Cortex-A35 in-order): puntos calientes de GDScript

Análisis **read-only** sobre `Odisea_Game/src` (árbol principal, commit de hoy) y el
motor/fork en `godot-box3d-3` + `godot` (fuente del engine 3.6). Perfil de referencia:
`replay_perf.json` de la corrida RingHub_Level en el binario shipeado (Rocknix/RG351V:
RK3326, 4× A35 in-order, 1 GB, GLES3), 455 ticks, ~20 fps.

No se aplicó ningún cambio. Este documento es la propuesta; los diffs son de forma
(`diff`-shaped), sin aplicar.

---

## 0. Cómo leer el perfil (qué mide cada etiqueta)

`PerformanceMonitor.perfil_inicio/fin` (`core_v2/autoloads/PerformanceMonitor.gd:538-551`)
acumula `usec` y `llamadas` por clave String, y `perfil_corrida_terminar()` (`:523-536`)
publica `ms_por_llamada = usec / max(1, llamadas) / 1000`. Para etiquetas que corren
**una vez por tick** (`· scripts del tick`, `SessionManager`, `PC.move`, …) `ms/call ==
ms/tick`. Para `Interactables` (955 llamadas / 455 ticks ≈ 2.1 por tick) el total por tick
es `0.33 × 2.1 ≈ 0.7 ms`, no 0.33.

Los tramos anidados (el llamador incluye al llamado) están en:

- `core_v2/systems/PerfilTickSentinela.gd:33-47` — sentinelas `process_priority ±10000`
  que encierran todos los `_physics_process` (`· scripts del tick`, 12.55) y todos los
  `_process` (`· scripts del frame`, 3.22).
- `core_v2/autoloads/SessionManager.gd:1582,1877` — `SessionManager` (8.57) envuelve todo
  el `_physics_process` del autoload.
- `core_v2/autoloads/SessionManager.gd:1762,1815,1822,1824,1829,1842` — `SM.bookkeeping`
  (0.62) = `_check_events_for_frame`; `SM.player_step` (6.39) = `player.step()`; `SM.sync_nodes`
  (0.89) = bucle de `node.step()`.
- `core_v2/player/PlayerControllerV2.gd:2620-2621,2706-2709,2755,2765,2768,2817,2843,2858-2863,2911,2946`
  — `PC.control`, `PC.move`, `PC.move.pre*`, `PC.move.slide`, `PC.post`.
- `core_v2/camera/KinematicArm3D.gd:268-271` — `KinematicArm3D` (0.74).
- `core_v2/components/InteractableBaseV2.gd:390-393` — `Interactables`.

Reparto del tick (ms/tick, medido):

```
· scripts del tick                12.55     <- todo el GDScript de física
  SessionManager                   8.57     <- wrapper del autoload
    SM.player_step                 6.39       (player.step)
      PC.control                   0.94
      PC.move                      4.33
        PC.move.pre                1.78
          PC.move.pre.stepup       1.07     <- _try_step_up
        PC.move.slide              2.02     <- move_and_slide_with_snap (nativo)
        (post dentro de move ~0.53)
      PC.post                      0.79
    SM.sync_nodes                  0.89
    SM.bookkeeping                 0.62     <- _check_events_for_frame
    (resto del wrapper ~0.67)
  KinematicArm3D                   0.74
  Interactables                    0.33 ×~2.1 llamadas/tick  (~0.7/tick)
  CollisionCullManager             ~0    (se autodesactiva en Box3D, FD-290)
servidor de física                 18.2
process (frame)                    33.4
```

Conclusión estructural: **el tick de GDScript son ~12.5 ms y el servidor de física ~18.2 ms
sobre un presupuesto de 33 ms a 30 Hz**. Lo que se puede recortar desde GDScript es del orden
de 1.5–2.5 ms/tick (no el 18 ms del servidor). Aun así, 1.5–2.5 ms es ~5–8 % del frame y
recupera margen para el tick de física en el handheld.

> Nota de medición: `perfil_inicio/fin` usa `OS.get_ticks_usec()` + claves String en
> diccionario **dentro** del tramo medido, así que las cifras absolutas están infladas por
> el propio trazador. Las comparaciones relativas entre etiquetas siguen siendo válidas; no
> lo son para juzgar un `_check_events_for_frame` de 0.62 ms (ver §4).

---

## 1. Tabla rankeada

Orden = (ms/tick ahorrados) / (esfuerzo + riesgo). "est." = estimación propia a partir del
perfil; sólo `PC.move.pre.stepup = 1.07` y `PC.move.slide = 2.02` son mediciones directas.

| # | sistema / función | file:line | issue | fix | est. ms/tick | esfuerzo | riesgo |
|---|---|---|---|---|---|---|---|
| 1 | `SM.sync_nodes` (filtro por tick) | `SessionManager.gd:1830-1842`, `:207-220` | por cada nodo sync, cada tick: `player.is_a_parent_of(node)` (recorre el árbol) + `node.has_method("step")` (lookup String) | precomputar la lista de nodos con `step` y sin el player como ancestro al construir el cache (ya se invalida por señales); iterar esa lista en el tick | 0.3–0.5 | S | bajo |
| 2 | `PC.move.pre.stepup` | `PlayerControllerV2.gd:3267-3336` (`3304`, `3292`, `3312`, `3325`) | hasta 4 `move_and_collide` (test_only) por tick, cada uno = depenetración + `b3World_CastShape` | reemplazar el barrido de techo (cápsula, `3304`) por 1 `intersect_ray` a la altura del casco; mantener la certificación `_step_clear_dist`; no tocar el swap de máscara (bajo Box3D es casi gratis, §4) | 0.2–0.4 | M | medio (semántica de sonda) |
| 3 | Wrapper de `SessionManager` | `SessionManager.gd:1750`,`:1633`,`:254-257`; `:1870`; `:1601-1604`; `:1608`,`:1618` | por tick: `get_node_or_null("/root/SceneManager")` en `_is_scene_transitioning`, `get_node_or_null("/root/VideoExporter")`, `_find_player()` con `is_a_parent_of`, 2× `Input.is_action_just_pressed` | cachear `SceneManager`/`VideoExporter` como ya se cachea `_pm_prof` (`:1572-1580`); validar player contra `current_scene` cacheado; gatear los `is_action_just_pressed` por modo | 0.2–0.4 | S | bajo |
| 4 | `Interactables` | `InteractableBaseV2.gd:192-193`,`:209-210`,`:227-228` | 2 `has_method("measure_start"/"measure_end")` por `step()` (~2.1 llamadas/tick) | cachear `_perf_detailed` una vez en `_ready` y gatear con un bool | 0.1–0.3 | S | bajo |
| 5 | `KinematicArm3D` allocs | `KinematicArm3D.gd:413`,`:626`,`:667` (`:336`,`:386`,`:472`) | `PhysicsShapeQueryParameters.new()` por cast (1–3 por tick) y varias lecturas de `global_transform` | reusar un `PhysicsShapeQueryParameters` miembro (mutar `transform`/`collision_mask`); cachear pose | 0.1–0.2 | S | bajo |
| 6 | 3 barridos de `get_slide_collision()` frescos | `PlayerControllerV2.gd:2887-2901`, `:3139-3144`, `:3187-3194` | `_update_floor_info`, `_update_platform_tracking` y el empuje a rígidos re-iteran el mismo set de slides tras `move_and_slide` | unificar en una sola pasada y pasar el resultado | 0.05–0.15 | S | medio (orden/semántica) |
| 7 | Tipado estático en caliente | `PlayerControllerV2.gd:2473`, `:3267`, `:1223`, `:3146`, `:3135`; `KinematicArm3D.gd:275` | `dt`, params y locales sin tipo → chequeos/boxing Variant en la VM | tipar params/retornos/locales (`dt: float`, `-> Dictionary`, `var origin: Vector3`), usar `:=` | 0.2–0.8 (incierto) | L | medio (parse/regresión) |
| 8 | Doble `get_overlapping_bodies()` del InteractArea | `PlayerControllerV2.gd:2208` (`_process_interaction`) y `:2363` (`_update_push_state`) | la misma Area se consulta en dos caches con throtles distintos (8 y 8 ticks, fase distinta) | compartir un único cache de overlaps por tick con el mismo contador | 0.05–0.15 | S | bajo |
| 9 | `move_and_slide_with_snap` | `PlayerControllerV2.gd:2860` | 2.02 ms/tick nativos; snap + hasta `max_slides=4` barridos | A/B: `max_slides` 4→3 y/o snap sólo cuando `is_on_floor()`; NO cambiar sin re-grabar replays | 0–0.5 (incierto) | M | alto (replay/feel) |
| 10 | Luces/beacons de `step` continuo | `FluorescentLight.gd:99-139`, `EmergencyBeaconV2.gd`, `IndustrialFan.gd:28` | `_update_visuals` escribe `light_energy`/transform por tick (~2.1 props/tick, 0.33 ms/llamada) | throtlear a 1 de cada 2–3 ticks (los parpadeos son deterministas sobre `_time_acc`) o mover a shader | 0.1–0.3 | M | medio (look) |
| 11 | `range()` en bucles de construcción | `FakeShadow.gd:535-567,685-686`; `GasParticleManager.gd:486-514` | `range()` asigna `Array` (`gdscript_functions.cpp:1883-1885`) | `for i in n` (INT, sin alloc; `variant_op.cpp:2879-2882`) | ~0 | S | nulo |
| 12 | Cachear refs de autoload en locales | `PlayerControllerV2.gd:1226-1233,1310,1339` | accesos repetidos a `CinematicManager` por tick | `var cine = CinematicManager` una vez por `step()` | ~0 en release | S | nulo |

> El rank 12 sólo rinde en builds **con tools** (ver §4): en export `TOOLS_ENABLED` está
> apagado y los autoloads se resuelven como índice de array global
> (`gdscript_compiler.cpp:329`), no por el mapa de nombres (`gdscript_function.cpp:106-118`).

---

## 2. Detalle por ítem (código actual y cambio propuesto)

### 2.1 — `SM.sync_nodes`: filtrar el grupo una vez, no cada tick

Actual (`SessionManager.gd:1829-1842`):

```gdscript
if _perf_fino: pm.perfil_inicio("SM.sync_nodes")
var sync_nodes = _get_replay_sync_nodes()
for node in sync_nodes:
    if node != player and (not is_instance_valid(player) or not player.is_a_parent_of(node)) and node.has_method("step"):
        if _perf_fino:
            var clave: String = _sync_step_clave(node)
            pm.perfil_inicio(clave)
            node.step(FIXED_DT)
            pm.perfil_fin(clave)
        else:
            node.step(FIXED_DT)
if _perf_fino: pm.perfil_fin("SM.sync_nodes")
```

`_get_replay_sync_nodes()` (`:207-220`) ya cachea el **grupo**, pero el filtro
`is_a_parent_of`/`has_method` se repite en cada tick (y el mismo patrón está en `:1725-1727`
durante grabación). Propuesta: que el cache guarde ya los nodos "steppeables" y sin el player
como ancestro, y reconstruir sólo cuando el cache está sucio (ya existe
`_replay_sync_cache_dirty`, `:208`, `:246`, `:250`):

```diff
 func _get_replay_sync_nodes() -> Array:
 	if _replay_sync_cache_dirty:
 		var all_nodes = get_tree().get_nodes_in_group("replay_sync")
 		var active_scene = _get_active_scene_root()
 		var filtered = []
 		for node in all_nodes:
 			if not is_instance_valid(node):
 				continue
 			if not is_instance_valid(active_scene) or active_scene == node or active_scene.is_a_parent_of(node):
 				filtered.append(node)
 		filtered.sort_custom(self , "_sort_nodes_by_path")
 		_replay_sync_cache = filtered
+		# lista de paso: sólo nodos con step(), sin el jugador como ancestro.
+		# player puede cambiar después de construir el cache (respawn), así que
+		# esto se revalida barato en el tick: no hay is_a_parent_of por nodo.
+		var step_nodes := []
+		for node in filtered:
+			if node.has_method("step"):
+				step_nodes.append(node)
+		_replay_sync_step_cache = step_nodes
 		_replay_sync_cache_dirty = false
 	return _replay_sync_cache
```

y en el tick:

```diff
-	for node in sync_nodes:
-		if node != player and (not is_instance_valid(player) or not player.is_a_parent_of(node)) and node.has_method("step"):
+	for node in _replay_sync_step_cache:
+		if node != player and (not is_instance_valid(player) or not player.is_a_parent_of(node)):
```

**Efecto:** quita 1 `has_method` (String) por nodo sync por tick; deja el `is_a_parent_of`
sólo si el player existe (se puede precomputar también, marcando los descendientes del player
en el rebuild). **Riesgo:** si el player se reparenta bajo un nodo sync en runtime, la lista
cacheada quedaría vieja — hoy el mismo nodo ya se revalida por instancia, así que el riesgo es
bajo pero conviene mantener el chequeo de `player` como en el diff.

### 2.2 — `_try_step_up`: bajar de 4 barridos a 3 (o menos)

Actual (`PlayerControllerV2.gd:3292-3306`):

```gdscript
var foot_collision = move_and_collide(move_dir * probe_distance, true, true, true)
if foot_collision == null or foot_collision.normal.y > 0.7:
    _step_clear_origin = origin
    _step_clear_dir = move_dir
    _step_clear_dist = probe_distance
    can_try_step = false
else:
    _step_clear_dist = 0.0

if can_try_step:
    var head_collision = move_and_collide(Vector3.UP * step_height, true, true, true)
    if head_collision != null:
        can_try_step = false
```

El chequeo de techo es un barrido de cápsula completo (`b3World_CastShape` sobre el
`body_test_motion` del KinematicBody, `box3d_motion.cpp:886-943`). Para descartar un escalón
por falta de altura alcanza con una línea vertical a la altura del casco. Propuesta
(no aplicada):

```diff
 if can_try_step:
-    var head_collision = move_and_collide(Vector3.UP * step_height, true, true, true)
-    if head_collision != null:
-        can_try_step = false
+    var head_from := origin + Vector3.UP * (crouch_headroom_margin)
+    var head_to := head_from + Vector3.UP * step_height
+    var head_hit := get_world().direct_space_state.intersect_ray(
+        head_from, head_to, [self], _get_step_support_collision_mask())
+    if not head_hit.empty():
+        can_try_step = false
```

**Riesgo:** una línea puede pasar entre dos volúmenes donde la cápsula no; subiría algún
falso "hay hueco". Hay que A/B contra `test_locomocion_walk`/`test_salto_vertical` porque la
altura de escalón afecta trayectoria. Si el A/B no es concluyente, dejar la cápsula.

Lo que **no** hay que tocar: el swap de `collision_mask` (`:3286-3287`, `:3335`). La
hipótesis de que cuesta es falsa bajo Box3D (§4): `Box3DBody::apply_filter()`
(`box3d_objects.cpp:654-666`) escribe `maskBits` constante y `categoryBits = collision_layer`;
cambiar sólo `collision_mask` no cambia el `b3Filter` y `b3Shape_SetFilter`
(`thirdparty/box3d/src/shape.c:1385-1398`) retorna temprano sin destruir contactos. El costo
de `_try_step_up` son los barridos, no la máscara.

### 2.3 — Wrapper de `SessionManager`: lookups por path en cada tick

Actual (`SessionManager.gd:253-257`):

```gdscript
func _is_scene_transitioning() -> bool:
	var sm = get_node_or_null("/root/SceneManager")
	if sm == null:
		return false
	return sm.has_method("is_transitioning") and sm.is_transitioning()
```

Se llama por tick en replay (`:1750`) y grabación (`:1633`). Igual patrón en `:1870`:

```gdscript
var __ext_exporter = get_node_or_null("/root/VideoExporter")
if __ext_exporter != null and __ext_exporter.is_exporting:
```

Propuesta (mismo patrón ya usado para `_pm_prof`, `:1572-1580`):

```diff
+var _scene_manager_cache = null
+var _scene_manager_buscado := false
+var _video_exporter_cache = null
+var _video_exporter_buscado := false
+
 func _is_scene_transitioning() -> bool:
-	var sm = get_node_or_null("/root/SceneManager")
-	if sm == null:
-		return false
-	return sm.has_method("is_transitioning") and sm.is_transitioning()
+	if not _scene_manager_buscado:
+		_scene_manager_buscado = true
+		_scene_manager_cache = get_node_or_null("/root/SceneManager")
+	var sm = _scene_manager_cache
+	if sm == null:
+		return false
+	return sm.is_transitioning()
```

```diff
-	var __ext_exporter = get_node_or_null("/root/VideoExporter")
-	if __ext_exporter != null and __ext_exporter.is_exporting:
+	if not _video_exporter_buscado:
+		_video_exporter_buscado = true
+		_video_exporter_cache = get_node_or_null("/root/VideoExporter")
+	var __ext_exporter = _video_exporter_cache
+	if __ext_exporter != null and __ext_exporter.is_exporting:
```

`_find_player()` (`:1601-1604`) por tick hace `_is_player_candidate_valid` → `get_tree().current_scene`
+ `scene.is_a_parent_of(p)`, que recorre el árbol hasta la raíz cada tick. Propuesta: cachear
`current_scene` y comparar por `get_tree().current_scene == _scanned_scene` como ya hace
`CollisionCullManager._on_tree_changed` (`CollisionCullManager.gd:146`), revalidando el player
sólo al cambiar de escena. `Input.is_action_just_pressed("skip"/"record-toggle")` (`:1608`,`:1618`)
se puede gatear: `skip` sólo si `oys_interpreter.is_running`, y `record-toggle` sólo en builds
con grabación habilitada.

### 2.4 — `Interactables`: `has_method` por `step()`

Actual (`InteractableBaseV2.gd:191-228`):

```gdscript
func step(dt: float) -> void:
	if _perf_monitor and _perf_monitor.has_method("measure_start"):
		_perf_monitor.measure_start(self , "step")
	...
		if _perf_monitor and _perf_monitor.has_method("measure_end"):
			_perf_monitor.measure_end(self , "step")
		return
	...
	if _perf_monitor and _perf_monitor.has_method("measure_end"):
		_perf_monitor.measure_end(self , "step")
```

`measure_start`/`measure_end` ya retornan temprano si `_detailed_node_profiling_enabled`
es false (`PerformanceMonitor.gd:316-319`), que es el default (`:57`,`:71`). El costo es
justamente el `has_method` (lookup String) ×2 por llamada. Propuesta:

```diff
+var _perf_detailed := false
...
 func _ready():
+	_perf_detailed = _perf_monitor != null and bool(_perf_monitor._detailed_node_profiling_enabled)
...
 func step(dt: float) -> void:
-	if _perf_monitor and _perf_monitor.has_method("measure_start"):
+	if _perf_detailed:
 		_perf_monitor.measure_start(self , "step")
...
-		if _perf_monitor and _perf_monitor.has_method("measure_end"):
+		if _perf_detailed:
 			_perf_monitor.measure_end(self , "step")
```

**Nota:** esto sólo quita el overhead de instrumentación; el grueso de 0.33 ms/llamada está
en `_update_visuals()` de las subclases con `_wants_continuous_step()==true`
(`FluorescentLight.gd:45`, `EmergencyBeaconV2.gd:230`, `EmergencyBeaconSpotV2.gd:215`,
`IndustrialFan.gd:28`, `RadiatorProp.gd:109`, `HoloTerminalV2.gd:533`, `ElevatorController.gd:251`,
`ElevatorFloorSelector.gd:659`, `HoldInteractableV2.gd:58`, `DebugConsoleHUD.gd:74`). Ahí el
cambio es throttle/port a shader (ítem 10, needs measurement).

### 2.5 — `KinematicArm3D`: reusar `PhysicsShapeQueryParameters`

Actual (`KinematicArm3D.gd:413-422`, `:626-632`, `:667-673`): tres sitios hacen
`var params := PhysicsShapeQueryParameters.new()` por llamada; en el camino común corren
`_cast_shape_safe_fraction` (`:336`) y, si el jugador se mueve por encima de
`collision_motion_lookahead_min_speed`, `_cast_motion_lookahead_hit_length` → `_cast_shape_hit_length`
→ `_cast_shape_safe_fraction` (`:386,472`). Propuesta (una instancia miembro):

```diff
+var _query_params := PhysicsShapeQueryParameters.new()
...
 func _cast_shape_safe_fraction(arm_origin: Vector3, direction: Vector3, probe_length: float) -> float:
 	...
-	var params := PhysicsShapeQueryParameters.new()
-	params.set_shape(collider_shape)
-	params.transform = Transform(global_transform.basis, arm_origin)
-	params.collision_mask = collision_mask
-	params.exclude = _excluded_objects
+	var params := _query_params
+	params.shape = collider_shape
+	params.transform = Transform(global_transform.basis, arm_origin)
+	params.collision_mask = collision_mask
+	params.exclude = _excluded_objects
```

**Riesgo:** nulo si se reasigna todo lo relevante en cada uso. Cuidado con reentrancia:
`_cast_shape_safe_fraction` no se llama a sí misma, así que una única instancia alcanza.

### 2.6 — Un solo barrido de slides

`PlayerControllerV2.gd` recorre `get_slide_count()`/`get_slide_collision(i)` cuatro veces por
tick: `_standing_on_moving_terrace()` (`:3228`, con el set del tick anterior, se llama en
`:2837`), y después de `move_and_slide` (`:2860`) lo hacen `_update_floor_info()` (`:3139`),
`_update_platform_tracking()` (`:3187`) y el empuje a `RigidBody` (`:2887`). Los tres últimos
miran el mismo set fresco. Propuesta: tras `move_and_slide`, armar una vez
`var _slides := []` con `{collision, collider, normal}` y pasarla a las tres funciones.
**Riesgo:** el orden importa (`_update_floor_info` fija el normal que usa
`_get_support_normal()` en el empuje); hay que preservarlo.

### 2.7 — Tipado estático

`GDScriptDataType` (`gdscript_function.h:45-90`) hace que params/retornos/locales tipados
eviten chequeos y boxing Variant en la VM. Los candidatos de mayor relación beneficio/riesgo
son funciones chicas y muy llamadas:

- `PlayerControllerV2.gd:2473` `func step(dt: float, input: InputDataV2) -> void` (ya tipado) —
  tipar los locales `var input = null` → `var input: InputDataV2`, `var cm = _cm_cache` →
  `var cm: Node`.
- `PlayerControllerV2.gd:3267` `_try_step_up(motion: Vector3) -> Dictionary` y sus locales.
- `PlayerControllerV2.gd:1223` `_get_move_direction(...)` → tipar `world_dir`, `res`.
- `PlayerControllerV2.gd:3135` `_update_floor_info()`, `:3146` `is_effectively_grounded() -> bool`.
- `KinematicArm3D.gd:275` `_paso_fisica(delta)` → `delta: float`.

**Riesgo:** medio (un error de tipo en una función caliente cambia semántica). Hacer en PR
separado y medir con `replay_perf.json` antes/después. En 3.6 `var x: T` y `:=` ya están
soportados.

### 2.8 — Doble query del InteractArea

`_get_interaction_overlaps()` (`:2204-2210`) y `_update_push_state` (`:2362-2363`) llaman
`_interact_area.get_overlapping_bodies()` por separado, con throtles de 8 ticks en distinta
fase; en el peor caso son 2 consultas de solape/tick más `_interact_area.get_overlapping_areas()`.
Propuesta: un único cache por tick compartido (mismo contador de scan). **Riesgo:** bajo;
el scan de push sólo cambia la selección de target, no la dirección/empuje del frame.

### 2.9 — `move_and_slide_with_snap` (sólo A/B)

`PlayerControllerV2.gd:2860`:

```gdscript
velocity = move_and_slide_with_snap(velocity, snap_vec, UP, true, 4, deg2rad(45), false)
```

Es 2.02 ms/tick **nativos** (`body_test_motion` → depenetración + `b3World_CastShape` por
forma, `box3d_motion.cpp:886-943`); no hay forma de bajarlo en GDScript más que reducir
iteraciones. `deg2rad(45)` se recalcula por llamada (micro). Opciones a A/B: `max_slides` 4→3;
pasar `snap_vec = Vector3.ZERO` cuando `is_on_floor()` y `velocity.y` no sea negativo
(el snap baja por las escaleras; quitarlo cambia el feel y **la trayectoria**). Requiere
re-grabar replays. **No** aplicar sin medir.

---

## 3. Quick wins (bajo riesgo, ahorro claro)

1. **Cachear `SceneManager` y `VideoExporter` en `SessionManager`** (§2.3) — mismo patrón ya
   usado para `PerformanceMonitor`; cero cambio de comportamiento. 0.1–0.2 ms/tick.
2. **Precomputar la lista de nodos sync con `step`** (§2.1) — el cache ya se invalida por
   señales; sólo se mueve trabajo del tick al rebuild. 0.3–0.5 ms/tick.
3. **Gate de `InteractableBaseV2.step` por `_perf_detailed`** (§2.4) — quita 2 `has_method`
   por llamada. 0.1–0.3 ms/tick.
4. **Reusar `PhysicsShapeQueryParameters` en `KinematicArm3D`** (§2.5) — elimina 1–3
   `Object` por tick. 0.1–0.2 ms/tick.
5. **Gatear `Input.is_action_just_pressed("record-toggle")`** (`SessionManager.gd:1618`) a
   builds con grabación — el dispositivo no graba en gameplay. ~0.02–0.05 ms/tick.

Total quick wins estimado: **~0.6–1.2 ms/tick**.

## 4. Medido vs asumido

**El perfil ya prueba como calientes** (ms/call medidos en 455 ticks):
`· scripts del tick` (12.55), `SessionManager` (8.57), `SM.player_step` (6.39),
`PC.move` (4.33), `PC.move.slide` (2.02), `PC.move.pre` (1.78), `PC.move.pre.stepup` (1.07),
`PC.control` (0.94), `SM.sync_nodes` (0.89), `PC.post` (0.79), `KinematicArm3D` (0.74),
`SM.bookkeeping` (0.62), `Interactables` (0.33 ×955 llamadas). Los ítems 1–6 y 8 nacen de
esas etiquetas.

**Inferido, requiere A/B en device:**
- Ahorro real del fix de §2.1/§2.3/§2.4/§2.5 (los submétodos que están *dentro* de la
  etiqueta medida no están separados en el perfil).
- Tipado estático (§2.7): plausible en la VM de 3.6 (`GDScriptDataType`), sin cota medida.
- Luces/beacons (§2.10): el 0.33/llamada de `Interactables` mezcla `has_method` +
  `_update_visuals`; hay que instrumentar `_update_visuals` por subclase.
- `move_and_slide` (§2.9): cambio de comportamiento, no de código.

**Refutado contra el código/engine (no insistir):**
- **El swap de `collision_mask` de `_try_step_up` NO destruye contactos.** `Box3DBody::apply_filter()`
  (`box3d_objects.cpp:654-666`) fija `maskBits` constante y `categoryBits = collision_layer`;
  cambiar sólo `collision_mask` no altera el `b3Filter`, y `b3Shape_SetFilter`
  (`shape.c:1385-1398`) compara y retorna temprano. Coincide con `set_collision_mask`
  (`scene/3d/collision_object.cpp:117-123`) llamando igual a `body_set_collision_mask`. El
  costo del 1.07 ms son los barridos, no la máscara. (La línea `apply_filter()` sí es real
  para cambios de `collision_layer`.)
- **`SM.bookkeeping = 0.62 ms/tick` no cuadra con el código.** `_check_events_for_frame`
  (`SessionManager.gd:2883-2887`) es un `Dictionary.has(int)` sobre un dict casi vacío. El
  0.62 casi seguro mide el propio `perfil_inicio/fin` (ticks + hash String) o hay eventos
  densos en RingHub; no lo tomes como objetivo sin desglosarlo.
- **El ~0.67 ms de `SessionManager` sin etiqueta hija** incluye `_muestrear_perf()`
  (`:2677-2694`), que corre **sólo** cuando la traza está encendida y arma un Dictionary con
  6 `Performance.get_monitor()` por frame. Es overhead del trazador: no existe en una partida
  normal. Al comparar contra `ms_physics` hay que recordar que el perfil se auto-infla.

## 5. Veredicto sobre `docs/odisea-box3d.md` ("Fruta madura")

El documento es anterior a FD-290 y **varias de sus propuestas ya están implementadas**;
otras citan líneas que ya no coinciden. Punto por punto, contra el código de hoy:

| Claim del doc | Estado al 2026-09-25 | Evidencia |
|---|---|---|
| #1 Retirar/atenuar `CollisionCullManager` | **Implementado**: se autodesactiva con backend Box3D | `CollisionCullManager.gd:116-127` (FD-290) |
| #2 `FakeShadow` grid 64 RayCast | **Obsoleto**: ya no hay nodos RayCast; una sola pasada `intersect_ray` sobre offsets precomputados; en LOW cheap/disable | `FakeShadow.gd:94-102`, `:392-396`, `:521-570`; `GLES3VendorGate.gd:147-149` (`ODISEA_DISABLE_FAKE_SHADOW=1`) |
| #3 Trimesh de colisión compartidos | **Implementado**: cache por `Mesh` vía meta | `ShapeBounds.gd:23-37` |
| #5 `KinematicArm3D` colapsar barridos | **Implementado**: 1 `cast_motion` compartido para hit+lookahead | `KinematicArm3D.gd:324-336`, `:403-425` |
| #6 `GasParticleManager` raycast por partícula | **Implementado**: presupuesto K por tick | `GasParticleManager.gd:474-514` |
| #7 Cachear lookups por tick (`SessionManager.gd:1528`, `PlayerControllerV2.gd:3122`) | **Ya cacheado**: `_pm_prof` y `_sm_cache`; las líneas citadas ya no corresponden | `SessionManager.gd:1572-1580`; `PlayerControllerV2.gd:2483-2489`, `:3367-3372`; `:2260-2262` |
| #8 sub-steps/workers | Sigue válido como guía, no es GDScript | — |

Nuevo respecto del doc (lo que el perfil agrega): `_try_step_up` es el mayor sub-costo
GDScript medido (1.07 ms/tick) y no estaba en la lista; `SM.sync_nodes` (0.89) y el wrapper
de `SessionManager` (~0.67 + instrumentación) tampoco.

---

## 6. Riesgos y orden sugerido de aplicación

1. Quick wins §2.1, §2.3, §2.4, §2.5 (bajo riesgo) — un PR, medir con el mismo
   `RingHub_Level` y comparar `replay_perf.json` frame a frame.
2. §2.8, §2.6 (unificar queries) — PR aparte, verificar `test_locomocion_walk`,
   `test_salto_vertical`, `test_push_integration`.
3. §2.2 (`_try_step_up`) — A/B obligatorio: la altura de escalón es trayectoria; si
   `test_locomocion_walk`/`test_salto_desplazamiento` derivan, revertir.
4. §2.7 (tipado) — PR grande, separado por función; sin él no hay número que valga.
5. §2.9/§2.10 — sólo con A/B y re-grabación de replays si se toca física.

Recordatorio de determinismo: cualquier cambio en #2 (sondas de step-up), #6 (orden de
captura de slide normals) o #9 (snap/max_slides) altera trayectorias y exige re-validar los
`.oys` de física antes de confiar en un ahorro medido en fps.
