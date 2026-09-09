# Odisea + Box3D: fruta al alcance de la mano

Notas para sacar rendimiento a Odisea sobre el backend Box3D
(`icarito/godot-box3d-3`), escritas desde el lado del módulo tras un estudio
de código de Odisea, del binding alternativo para Godot 4
(Stink-O/box3d-godot) y del propio módulo. Cada punto cita archivo y línea
del estado de `Odisea_Game/src` de hoy; los números de milisegundos citados
vienen de `core_v2/autoloads/CollisionCullManager.gd` (Redmi Note 9 Pro,
GLES2), el perfil más restrictivo del proyecto.

## Lo que ya ganas al migrar, sin tocar nada

- **Broadphase estático**: el 58 % del tick móvil (7.81 ms de ~14 ms) era
  coste del servidor de física sobre formas estáticas. Box3D mantiene las
  AABB de cuerpos estáticos "engordadas" y no las refit nunca: un nivel
  estático costará solo el bookkeeping del árbol. Al asentarse una escena,
  los cuerpos dormidos no se integran ni se despachan a nodos (ver
  `bench_settle` en el README: 1000 cajas dormidas ≈ 0.5 ms/frame con los
  1000 pares de contacto aún registrados).
- **Queries**: `intersect_ray` usa closest-hit sobre el BVH del motor con
  clipping temprano; `intersect_shape`/`cast_motion` van por proxy de nube de
  puntos. Los raycasts de FakeShadow, KinematicArm, gas y ANNA salen más
  baratos sin cambiar una línea de GDScript.
- **Determinismo**: sub-steps fijos single-threaded con `-ffp-contract=off`
  tanto en la librería como en el código del módulo. Es justo lo que la CI
  de Odisea valida (`determinism_tests.yml`).

## Fruta madura en Odisea (cambios pequeños, ganancia medida o directa)

1. **Retirar o atenuar `CollisionCullManager`**
   (`core_v2/autoloads/CollisionCullManager.gd:1-74`). Existía para
   amortiguar el broadphase de Bullet sobre 383 formas Prop. Con Box3D las
   formas estáticas no cuestan por iterar: el motivo de ser del manager
   desaparece. Su propio doc registra el bug que introduce (replay
   `test_locomocion_strafe` atraviesa un prop culleado, `:67-70`). Apagarlo
   elimina un `_physics_process` cada 8 frames *y* un error de física
   conocido. Medir: 383 formas Prop → ~10-14 ms del tick móvil.

2. **`FakeShadow` en modo `grid`**: 64 raycasts por actor cada 4 frames
   (`core_v2/visual/FakeShadow.gd:222`, `grid_resolution=8` en
   `Pilot_v2.tscn:246-257`), 0.33 ms medidos en móvil. Opciones, de menor a
   mayor esfuerzo:
   - Bajar `grid_resolution` a 5-6 en ARM (el fallback `cheap` ya existe,
     `:82-89`); con manta barata el faldón apenas se nota.
   - Migrar la grilla a **una sola query por frame** por actor usando
     `PhysicsDirectSpaceState.intersect_ray` en bucle sobre puntos pre-computados
     (sin nodos `RayCast` ni `force_raycast_update`): elimina la overheard de
     N nodos y consulta el mismo BVH. El módulo ya maneja este patrón de
     batch (ver `m5_queries`).
   - Cache de malla: `_generate_mesh` (`:282-531`) reconstruye con
     `SurfaceTool` cada refresh; la altura del terreno es estable entre
     frames — regenerar solo cuando algún rayo cambia de nivel de grilla más
     allá de `snap_amount`.

3. **Trimesh de colisión compartidos.** `Mesh.create_trimesh_shape()` crea
   un `ConcavePolygonShape` nuevo **por llamada**: en
   `DuctMazeStreamer.gd:466,515,551,691,745,1124` y
   `ScaffoldHubRing.gd:367` cada pieza del streamer levanta su propio BVH
   aunque el mesh se repita. Cachear un shape por `Mesh` recurso (p. ej. un
   `static var` en el streamer, o `ShapeBounds.gd:39` como punto de paso)
   divide la construcción entre instancias y baja el pico de carga del
   streaming de ductos. El módulo ya valida el compartir shapes (test
   `m14_shared_trimesh`).

4. **Bakes de colisión con primitivas cuando alcance.** Los bakes de domo ya
   tomaron esa decisión para criopods (`tools/bake_dome_intro_criopods.gd:219-222`).
   Generalizar: suelo/tuberías/andamio horneados como trimesh son el caso
   peor para BVH (2× triángulos por el doble winding). Donde la navegación
   sea plana, un `BoxShape`/compuesto horneado cuesta menos tanto en carga
   como en traversal. El merge de `bake_scaffold_walkways.gd` (colisión por
   sub-scene con shapes originales) es el patrón correcto: mantenerlo.

5. **`KinematicArm3D`: colapsar barridos**
   (`core_v2/camera/KinematicArm3D.gd:321,350,356`). El spring arm lanza hasta
   3 `intersect_shape` por tick (hit + lookahead + motion lookahead) más el
   chequeo de techo. Con `cast_motion` el módulo devuelve `closest_safe` en
   una pasada y el lookahead se puede derivar de esa fracción. 0.26 ms/tick
   hoy; debería bajar a ~1/3.

6. **`GasParticleManager`: raycast por partícula**
   (`core_v2/systems/gas/GasParticleManager.gd:483`). El gate de
   `raycast_min_speed` ya amortigua; falta un tope por tick: raycastear solo
   las K partículas más rápidas por frame y repartir el resto por turnos (el
   gas se mueve lento; nadie nota 2 frames de retraso). Las raycast de Box3D
   son más baratas que las de Bullet, pero siguen siendo O(BVH) por rayo.

7. **Cachear lookups por tick** (micro, pero se paga en cada replay):
   `SessionManager.gd:1528` busca `/root/PerformanceMonitor` en cada
   `_physics_process`, y `PlayerControllerV2.gd:3122` busca `SessionManager`
   por tick. El propio proyecto documenta el patrón correcto
   (`PipeCoolantRun.gd:109-112`).

8. **Ajuste fino al migrar**: en `project.godot`, `physics/3d/physics_engine`
   queda bajo `override.cfg`/export (PR #324). Dos settings a revisar:
   - `physics/3d/box3d_substeps` (default 2): subir si las pilas se sienten
     blandas; **ojo**, cambiar sub-steps cambia trayectorias → los replays
     `.oys` grabados deben re-grabarse tras cambiarlo.
   - `physics/common/max_physics_steps_per_frame=4` está bien; Box3D usa el
     step fijo del motor igual que Bullet.

## Roadmap del módulo con impacto en Odisea

Ideas ya identificadas (ver README "Roadmap"), en orden de valor para este
proyecto:

- **Trimesh one-sided opcional**: mitades el traversal y la memoria de BVH
  para la geometría horneada del domo (hoy el módulo duplica triángulos para
  las dos caras). Requiere una bandera por shape y saltar el espejo en cada
  query — es el único cambio grande pendiente del módulo.
- **Centering de height fields** (aún sin uso en Odisea, 0 usos de
  HeightMapShape).
- **Contact recycle distance**: exponer `b3World_SetContactRecycleDistance`
  como project setting para estabilizar pilas de props (PushableBox,
  FusionCore) y reducir regeneración de contactos.
- **Pre-sizing de capacidades del mundo** (existe en el binding Godot 4;
  el engine vendido v0.1.0 aún no lo expone — revisar al actualizar el
  submodule): evita reallocs a mitad de step en niveles cargados.
- **Stepping con workers opt-in**: rompe el determinismo que Odisea valida
  en CI; solo como setting opt-in para screenshots/benchmarks, nunca default.

## Cómo validar cada cambio

```bash
# módulo: 27 escenas de aceptación
GODOT=../godot/bin/godot.x11.opt.tools.64 scripts/test.sh

# módulo: benchmarks (despierto / asentado)
GODOT=../godot/bin/godot.x11.opt.tools.64 scripts/bench.sh
godot --path test_project --no-window res://bench/bench_settle.tscn

# Odisea: replay determinista individual (PASS 1 graba, PASS 2 re-simula)
cd /home/icarito/Proyectos/Odisea_Game/src
GODOT_BIN=<binario box3d> ./runtest.sh --oys test_locomocion_walk

# Odisea: suite completa vía pytest (delegate a gdunit)
GODOT_BIN=<binario box3d> ./runtest.sh
```

Estado de hoy: 27/27 escenas del módulo pasan; en Odisea, los replays de
física (`test_salto_vertical`, `test_salto_desplazamiento`,
`test_locomocion_walk`, `test_push_integration`, `test_push_clipping`)
pasan con Box3D. `test_cargol_basic` falla por migración (RigidBody
empujado): falla idéntico con y sin las optimizaciones de este repo — es un
issue de comportamiento Box3D-vs-Bullet a resolver aparte, no una regresión.
