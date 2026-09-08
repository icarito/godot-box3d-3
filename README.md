# godot-box3d-3

**Box3D** is a Godot 3.6.x custom module that integrates
[Box3D](https://github.com/erincatto/box3d) — Erin Catto's 3D physics engine,
the 3D sibling of Box2D — as an alternative 3D physics server, without forking
Godot.

The engine source stays untouched: this repo is only consumed through Godot's
`custom_modules` build option.

```text
godotengine/godot        (development dependency, branch 3.6)
icarito/godot-box3d-3    (this repo, evolves independently)
```

## Layout

```text
godot-box3d-3/
└── box3d/
    ├── SCsub                     # build script
    ├── config.py                 # module configuration
    ├── register_types.*          # registers the "Box3D" physics server
    ├── box3d_types.h             # math and type conversions
    ├── box3d_objects.*           # spaces, bodies, areas, joints behind the RIDs
    ├── box3d_physics_server.*    # Box3DPhysicsServer: PhysicsServer implementation
    ├── box3d_motion.cpp          # body_test_motion(), the move_and_slide query
    ├── box3d_queries.cpp         # PhysicsDirectSpaceState queries
    ├── box3d_events.cpp          # contacts, area monitoring, space overrides
    ├── box3d_joints.cpp          # Godot joints mapped onto Box3D joints
    ├── box3d_proxies.h           # point-cloud proxies shared by motion and queries
    └── thirdparty/box3d/         # git submodule: erincatto/box3d
```

## Requirements

- SCons 4.x and Python 3
- A C++14 compiler
- Platform dev packages for your target (e.g. on Linux/X11: `libx11-dev`,
  `libxinerama-dev`, `libxcursor-dev`, `libxrandr-dev`, `mesa-dev`,
  `libasound2-dev`, `pkg-config`)

## Building

Clone Godot next to this repo and checkout the 3.6 branch:

```bash
git clone --branch 3.6 https://github.com/godotengine/godot.git godot
```

Build passing this repo as a custom module:

```bash
cd godot
scons platform=x11 custom_modules=../godot-box3d-3
```

The resulting editor binary includes both the builtin `Bullet` server and the
new `Box3D` server.

## Selecting the Box3D backend

In your Godot project settings:

```
physics/3d/physics_engine = "Box3D"
```

If the setting is missing or empty, the engine's default server is used, so
existing projects are unaffected.

Note: the build writes `*.o` objects next to the submodule sources
(Godot's standard thirdparty layout); add `*.o` to
`.git/modules/box3d/thirdparty/box3d/info/exclude` to keep the submodule
status clean on fresh clones.

## Tests

Headless acceptance scenes run against a built engine binary:

```bash
GODOT=../godot/bin/godot.x11.tools.64 scripts/test.sh
```

| Test | Covers |
|------|--------|
| `m2_falling_box` | static/rigid bodies, gravity, resting contact |
| `m3_kinematic` | `move_and_collide`, `move_and_slide`, `is_on_floor`, normals, walls |
| `m4_shapes` | sphere, capsule, cylinder, convex polygon, trimesh, collision exceptions |
| `m5_queries` | `intersect_ray/point/shape`, `cast_motion`, `collide_shape`, `rest_info`, area queries |
| `m6_contacts` | contact monitoring, `body_entered`, direct state contact data |
| `m6_areas` | area monitoring, `body_entered/exited`, zero gravity space override |
| `m7_joints` | pin, hinge and slider joints through the stock node API |

## Status

Feature complete for the Godot 3 gameplay layer:

- **Shapes**: box, sphere, capsule, cylinder, convex polygon, concave polygon
  (trimesh, static only, front faces only like Godot), height map (static
  only; created off-origin it loses the centering offset). Plane, ray and
  soft body shapes stay unsupported and report an error, matching what Box3D
  can simulate.
- **Bodies**: static, rigid, kinematic, character; mass, damping, gravity
  scale, CCD, axis locks, forces/impulses, sleeping, collision layers/masks,
  collision exceptions (realized as Box3D filter joints), ray pickable, force
  integration callback (`_integrate_forces`).
- **Kinematic**: `body_test_motion` with depenetration, sweep and resting
  contact; `move_and_slide`/`move_and_collide`/`is_on_floor` all pass.
- **Queries**: `intersect_ray`, `intersect_point`, `intersect_shape`,
  `cast_motion`, `collide_shape`, `rest_info`, `get_closest_point_to_object_volume`,
  with `collide_with_bodies`/`collide_with_areas` handling.
- **Areas**: shapes on a kinematic sensor proxy, body and area monitoring
  callbacks (feeds `body_entered/exited`, `area_entered/exited`),
  monitorable, ray pickable, and space overrides (replace/combine gravity,
  damping, gravity points with distance falloff).
- **Contact reporting**: `max_contacts_reported` + direct state contacts
  (position, normal toward the body, impulse, shapes, collider) — the data
  behind RigidBody's contact monitor signals; per-space debug contacts.
- **Joints**: pin, hinge (limits + motor), slider (linear limits) and cone
  twist map onto Box3D spherical/revolute/prismatic joints. Generic 6DOF
  degrades to a weld with a warning: Box3D has no per-axis 6DOF equivalent.
- Soft bodies remain unsupported (Box3D is a rigid-body engine); every soft
  body call is a documented no-op.
- Single-threaded stepping (`workerCount = 1`) for determinism; Box3D's
  internal sub-stepping is used (4 sub-steps per frame).

Roadmap ideas: incremental shape rebuilds, height field grid centering,
threaded stepping once determinism is verified across runs.
