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

## Releases

Each tag publishes prebuilt binaries, so a project's CI does not have to
compile Godot:

| Artifact | What it is |
|----------|------------|
| `godot.box3d.linux.x86_64.headless` | `platform=server` build, links no X11 — the one a CI runner should call as `GODOT_BIN` |
| `godot.box3d.linux.x86_64.editor` | X11 editor binary, for working locally |
| `Godot-Box3D-export-templates-*.tpz` | Export templates, Linux and Windows x86_64, release and debug |

Consuming them:

```bash
# Test / import binary: whatever your CI calls as GODOT_BIN.
curl -sSLo godot-box3d -H "Accept: application/octet-stream"   "https://github.com/icarito/godot-box3d-3/releases/latest/download/godot.box3d.linux.x86_64.tools"
chmod +x godot-box3d

# Export templates, unpacked where the editor looks for them.
curl -sSLo templates.tpz   "https://github.com/icarito/godot-box3d-3/releases/latest/download/Godot-Box3D-export-templates-3.6.4.rc.custom_build.tpz"
mkdir -p ~/.local/share/godot/templates
unzip -q templates.tpz -d /tmp/tpl
mv /tmp/tpl/templates ~/.local/share/godot/templates/3.6.4.rc.custom_build
```

The template directory name must match the engine version string exactly or the
editor reports templates as missing. `scripts/build.sh` pins the Godot commit,
so that string is stable across builds of the same tag.

Exporting with the stock templates produces a game running Bullet, whatever the
project setting says: the backend only exists in binaries built with this
module. That is the whole reason the templates are published.

**Not yet published**: macOS, iOS, Android and HTML5 templates. Each needs a
toolchain the release workflow does not set up yet (SDK/NDK, Emscripten, an
Apple runner); the build recipe itself is platform-agnostic.

## Engine patches

The published binaries carry two fixes to Godot itself, applied by
`scripts/build.sh` before compiling — a std140 layout fix for the GLES3
directional light UBO, and an idempotent `make_dir_recursive()` on Android.
Neither touches this module. See [`patches/README.md`](patches/README.md).

## Building

This repo vendors Box3D as a git submodule, so clone it with the submodule or
the build fails on missing headers:

```bash
git clone --recurse-submodules https://github.com/icarito/godot-box3d-3.git
# already cloned without it:
git submodule update --init --recursive
```

Clone Godot next to this repo and checkout the 3.6 branch:

```bash
git clone --branch 3.6 https://github.com/godotengine/godot.git godot
```

Build passing this repo as a custom module:

```bash
cd godot
scons platform=x11 custom_modules=../godot-box3d-3
```

Or let `scripts/build.sh` do it, which is what CI runs: it pins the Godot
commit, applies the engine patches and builds the same artifacts the release
publishes.

```bash
scripts/build.sh editor                    # X11 editor, for scripts/test.sh locally
scripts/build.sh headless                  # server build, what CI runs
scripts/build.sh linux-templates           # export templates, release and debug
scripts/build.sh windows-templates         # cross-compiled, needs mingw-w64
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
| `m8_rays` | ray shapes: sweeps in `move_and_collide`, ray-feet `move_and_slide`, `is_on_floor` |
| `m9_trimesh_walk` | walking on a trimesh floor: grounded, rests at the surface, keeps moving |
| `m10_game_repro` | a real level's shape mix: kinematic walk, shadow rays, floor tracking |
| `m11_unstick` | leaving a prop the body already overlaps, and the surface past it |
| `m12_penetrated` | a body spawned inside geometry recovers and then walks away |
| `m13_trigger_layers` | area layer/mask filtering through `body_entered/exited` |
| `m14_shared_trimesh` | one trimesh resource shared by bodies at different origins |
| `m15_shape_scale` | scaled and mirrored shape transforms |
| `m16_area_reshape` | editing a shape resource reaches areas, not just bodies |
| `m17_cryopod` | a multi-shape prop blocks a walker |
| `m18_scaled_hull` | scaled convex hulls, and ray queries with body exclusions |
| `m20_body_scale` | body scale reaches the shapes at their visual position |
| `m21_character_mover` | a capsule character stays steady on props and slides along railings |
| `m22_trimesh_jitter` | a capsule on mesh geometry does not buzz, at either triangle winding |

## Documentation

- **This module**: this README is the setup and status reference; the module
  code comments (search for `ponytail:`) track known limitations.
- **Box3D engine** (`box3d/thirdparty/box3d/docs/`): upstream's own guide —
  `overview.md`, `collision.md`, `simulation.md` (sub-steps, determinism),
  `character.md`, `large_worlds.md`, `faq.md`.
- **Godot side**: everything is exposed through the standard Godot 3.6 3D
  physics classes (`RigidBody`, `StaticBody`, `KinematicBody`, `Area`,
  `PhysicsDirectSpaceState`, ...), so the engine manual applies unchanged.

## Setup / porting a project

1. Build the engine with this module (see *Building*).
2. In your project, select the backend:

   ```
   physics/3d/physics_engine = "Box3D"
   ```

3. That is all: the module implements Godot's `PhysicsServer`, so scenes use
   the stock nodes and there is no API to learn. Projects port by switching
   the setting; watch these caveats:
   - Trimesh (concave polygon) shapes only collide on static bodies. They do
     collide from both faces, like Godot's Bullet backend: Box3D itself treats
     a mesh triangle as one-sided, so sweeps against meshes are done triangle
     by triangle to stay facing-agnostic. Level geometry keeps whatever winding
     the modeller used.
   - Plane (WorldBoundary) and soft body shapes are unsupported. Ray shapes
     work as sensors for kinematic characters (sweeps + separation) but never
     generate contact response, which is Godot's own semantics.
   - Generic 6DOF joints degrade to welds; pin/hinge/slider/cone-twist map
     1:1.
   - Flat (zero-volume) convex polygon shapes work, but Box3D's hull builder
     needs a volume, so they are thickened by ~1 cm total along their plane
     normal. A plate collides as a very thin prism rather than a true plane.
   - Optional tuning: `physics/3d/box3d_substeps` (default 2, range 1–8).
     More sub-steps cost time and buy accuracy; 1 is closest to what Godot's
     Bullet backend does per frame.

## Troubleshooting

**Run from a terminal.** Every backend problem reports through `ERR_PRINT`, so
it lands on stdout and in the editor's Output dock. A project that "starts but
nothing collides" is almost always printing the reason:

```bash
../godot/bin/godot.x11.tools.64 --path /path/to/project 2>&1 | grep Box3D
```

To test a project without editing its `project.godot`, drop an `override.cfg`
next to it and delete it afterwards:

```ini
[physics]

3d/physics_engine="Box3D"
```

**Only the binary built with this module has the backend.** Selecting `"Box3D"`
in a stock Godot build silently falls back to Bullet.

**Rejected convex hulls.**

```
ERROR: Box3D: failed to build a convex hull from the given points, ignoring it.
ERROR: Box3D: failed to create shape type 6, ignoring it.
```

Box3D builds convex shapes with QuickHull, which cannot close a volume around a
point set that has none. Flat plates are handled (see *Setup / porting*), so
what is left here is genuinely unusable input: every point identical, or all of
them on one line. The shape is skipped and that collider stops existing while
the rest of the scene keeps working, which reads as "the level has no
collision". Check the shape's points before blaming the backend.

## Benchmarks

`scripts/bench.sh` runs the same 264-rigid-box stress scene headlessly under
each backend compiled into the engine, reporting the average
`Performance.TIME_PHYSICS_PROCESS` per frame over 300 frames after a
120-frame warmup.

Three runs on the reference machine, `target=release_debug`:

| Backend | avg physics ms/frame |
|---------|---------------------|
| Box3D, 1 sub-step | 0.93 – 1.56 |
| Box3D, 2 sub-steps (default) | 0.97 – 1.12 |
| Box3D, 4 sub-steps | 1.09 – 2.26 |
| Bullet (Godot's default 3D backend) | 1.18 – 1.75 |

- **Build optimised before judging anything.** The same scene costs 5 – 12 ms
  under `target=debug`, six to ten times more, because nothing is optimised
  and Box3D's own asserts are live. A debug build is for debugging.
- **Sub-steps are close to free at this scale.** 1, 2 and 4 all land near a
  millisecond. An earlier version of this file claimed 4 sub-steps cost twice
  what 1 does; that came from debug-build numbers, where the unoptimised
  solver exaggerates the difference. Sub-steps still buy solver quality, so
  raise `physics/3d/box3d_substeps` if stacks feel soft.
- **Box3D holds a small edge here**, with its default band under Bullet's
  across three runs. One scene on one machine is not a general claim about
  the two engines.

`INFO_ACTIVE_OBJECTS` and `INFO_COLLISION_PAIRS` report real values on Box3D;
the Bullet module returns zeroes for them, which is why its rows read 0.

## Building an optimised binary

```bash
cd godot
scons platform=x11 target=release_debug custom_modules=../godot-box3d-3
# -> bin/godot.x11.opt.tools.64, alongside the debug binary
```

## Status

Feature complete for the Godot 3 gameplay layer:

- **Shapes**: box, sphere, capsule, cylinder, convex polygon, concave polygon
  (trimesh, static only, both faces like Godot's Bullet backend), height map (static
  only; created off-origin it loses the centering offset) and ray shapes.
  Ray shapes have no contact surface (never rest, never push), matching
  Godot: they sweep in `move_and_collide` when ray shapes are not excluded,
  drive floor snapping, and drive `move_and_slide`'s ray separation
  (`is_on_floor` works for ray-feet characters). Plane and soft body shapes
  stay unsupported and report an error, matching what Box3D can simulate.
- **Bodies**: static, rigid, kinematic, character; mass, damping, gravity
  scale, CCD, axis locks, forces/impulses, sleeping, collision layers/masks,
  collision exceptions (realized as Box3D filter joints), ray pickable, force
  integration callback (`_integrate_forces`).
- **Kinematic**: `body_test_motion` with depenetration, sweep and resting
  contact; `move_and_slide`/`move_and_collide`/`is_on_floor` all pass, and
  ray shapes participate in sweeps and the ray separation phase like the
  Bullet backend's custom ray pairs.
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
  internal sub-stepping is used (2 sub-steps per frame by default).

Roadmap ideas: incremental shape rebuilds, height field grid centering,
threaded stepping once determinism is verified across runs.
