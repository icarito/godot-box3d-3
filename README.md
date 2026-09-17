# godot-box3d-3

This is the **Godot 3.6 fork Odisea ships on**: the engine, the platform
backends and the modules the game's builds are compiled with, published as
reproducible binaries so neither Odisea's CI nor a player has to compile Godot
itself.

It started as a couple of engine bugfixes that upstream did not take — a std140
layout fix for the GLES3 directional light UBO and an idempotent
`make_dir_recursive()` on Android — and grew into the stack those builds need:

- **Box3D physics** — a `Box3D` custom module: Erin Catto's
  [box3d](https://github.com/erincatto/box3d) as an alternative Godot 3.6 3D
  physics server, chosen per project without forking the engine.
- **Native Wayland** — the out-of-tree **FRT/SDL2** platform built with a
  desktop GL 3.3 core context, so a Wayland session runs the GLES3 renderer
  natively (EGL, no XWayland), compositor decorations and shader-compile
  keep-alive included.
- **FRT for handhelds** — the same platform cross-compiled for ARM64 with SDL2
  and OpenGL ES, which is what PortMaster handhelds (ROCKNIX and friends) can
  actually run.
- **Experimental GLES3 features** — the Godot 4 `Decal` node backported to the
  GLES3 renderer (`decal/` module, with blob-shadow demos) and the shader
  cache / asynchronous-compilation work the desktop editor leans on.

Upstream Godot source stays untouched: every engine change lives in `patches/`
applied over a pinned Godot commit, and the `box3d/` and `decal/` modules are
consumed through Godot's `custom_modules` build option.

```text
godotengine/godot        (pinned dependency, branch 3.6)
efornara/frt             (out-of-tree platform, cloned into platform/frt)
icarito/godot-box3d-3    (this repo: modules + patches, evolves independently)
```

## Layout

```text
godot-box3d-3/
├── box3d/                       # Box3D physics server (custom module)
│   ├── box3d_physics_server.*   # PhysicsServer implementation
│   ├── box3d_objects.*          # spaces, bodies, areas, joints behind the RIDs
│   ├── box3d_motion.cpp         # body_test_motion(), the move_and_slide query
│   ├── box3d_queries.cpp        # PhysicsDirectSpaceState queries
│   ├── box3d_events.cpp         # contacts, area monitoring, space overrides
│   ├── box3d_joints.cpp         # Godot joints mapped onto Box3D joints
│   └── thirdparty/box3d/        # git submodule: erincatto/box3d
├── decal/                       # Godot 4 Decal node backported to GLES3
│   ├── decal.cpp                # the node and its VisualServer wiring
│   ├── decal_editor_plugin.cpp  # editor gizmo/dock
│   └── demo_advanced/           # decal + blob-shadow demo scenes
├── patches/                     # engine + FRT patches over the pinned Godot
│   └── README.md                # what each patch does and why
├── scripts/                     # build.sh, the FRT toolchain, test helpers
└── test_project/                # headless acceptance scenes and benchmarks
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
| `godot.box3d.frt.linux.x86_64.editor` | FRT/SDL2 editor binary: same engine and module, SDL2 video. On a Wayland session SDL2 picks its native wayland driver (EGL/ES context, no XWayland); on X11 or the handheld CFWs it falls back to SDL2's x11/other backends. Implements `--no-window`. |
| `godot.box3d.frt.x86_64.release`, `godot.box3d.frt.x86_64.debug` | FRT/SDL2 x86_64 **runtime** templates (`tools=no`, desktop GL 3.3 core via EGL): the engine Odisea's Wayland-native Linux builds embed, the x86_64 counterpart of the arm64 handheld templates |
| `godot.box3d.thegates.linux.x86_64` | TheGates renderer: `platform=x11` template carrying the `the_gates` module (ZeroMQ IPC + shared-texture frame transport) alongside Box3D. The binary the TheGates launcher runs for gates declaring `godot_version = "3.6"` |
| `godot.box3d.thegates.linux.x86_64.debug` | Same renderer, `release_debug` build, for debugging a gate |
| `linux-3.6` | The renderer packaged the way TheGates' backend serves it: a zip whose archive root holds `Renderer-godot_v3.6.x86_64`, the exact file `/api/download_renderer/linux-3.6` returns |
| `godot.box3d.thegates.macos.universal`, `godot.box3d.thegates.windows.x86_64.exe` | The same renderer for macOS (arm64 + x86_64) and Windows (MinGW). Compile-verified; not yet run against a launcher on either |
| `macos-3.6`, `windows-3.6` | Their backend zips, holding `Renderer-godot_v3.6.universal` and `Renderer-godot_v3.6.exe` |
| `Godot-Box3D-export-templates-*.tpz` | Export templates for every platform Godot 3.6 targets: Linux x86_64 and ARM64, Windows x86_64, macOS universal, iOS, Android, HTML5 (threaded and not) |

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

Two things to know before shipping on 32-bit targets: ARMv7 falls back to
scalar SIMD, which is upstream's own call (ARMv7 NEON has no divide or sqrt),
and Box3D guarantees cross-platform determinism on 64-bit platforms only, which
leaves wasm32 and ARMv7 out. Both work; a simulation recorded on one will not
necessarily replay bit-for-bit on the other.

## Engine patches

Everything the fork changes in Godot lives in `patches/`, applied by
`scripts/build.sh` over a pinned commit before compiling (and `patches/frt/`
over the pinned FRT checkout). It is a mix of:

- **Upstreamable fixes** that upstream did not take: the std140 GLES3
  directional-light UBO layout, the idempotent `make_dir_recursive()` on
  Android, a `dynamic_font` outline-atlas leak, procedural-sky thread safety,
  and the GLES3 shader-cache / asynchronous-compilation and ubershader work.
- **Platform hooks** that teach the engine about `platform=frt` (all inert
  unless the target is FRT) and the desktop-GL build for `frt-editor`.
- **Wayland keep-alive and decorations**: the FRT window answers the
  compositor's pings while a synchronous shader compile blocks the main thread,
  and gets window-manager decorations instead of empty libdecor chrome.
- **`zzz_feature_decal_gles3.patch`**, a feature (not upstreamable): the Godot 4
  `Decal` node backported into the GLES3 renderer, paired with the `decal/`
  module. The scene shader sits at its 31-conditional limit, so custom defines
  there must be written `#if defined(...)`.
- **`zzzzz_feature_blob_shadow_gles3.patch`**, a feature (not upstreamable): the
  Godot 4 `BlobShadow`/`BlobFocus` nodes backported from upstream PR #84804 to
  both GLES3 and GLES2. Applied last because its `_render_list()` hunk lands on
  the diagnostics patch's `FRT_SKIN_NO_DEPTH` block; it frees the two version
  bits the sampler-budget patch was wasting so `USE_BLOB_SHADOWS` fits.

See [`patches/README.md`](patches/README.md) for the per-patch detail.

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
scripts/build.sh frt-editor                # FRT/SDL2 editor (Wayland nativo via SDL2)
scripts/build.sh frt-x86_64-templates      # FRT/SDL2 x86_64 runtime templates (Wayland)
scripts/build.sh linux-templates           # export templates, release and debug
scripts/build.sh windows-templates         # cross-compiled, needs mingw-w64 (-posix)
scripts/build.sh thegates-renderer         # TheGates browser renderer (x11 + the_gates)
scripts/build.sh thegates-renderer-macos   # same, macOS universal (needs Xcode)
scripts/build.sh thegates-renderer-windows # same, Windows (MinGW)
scripts/build.sh html5-templates           # needs emsdk
scripts/build.sh android-templates         # needs the SDK and NDK r23c
scripts/build.sh macos-templates           # needs Xcode
scripts/build.sh ios-templates             # needs Xcode
```

Run it with no arguments for the list.

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

## TheGates runtime

[TheGates](https://thegates.io) is a 3D internet browser whose launcher runs
each gate in a separate renderer process. Its Godot 3 support travelled as
[thegatesbrowser/thegates#1](https://github.com/thegatesbrowser/thegates/pull/1)
and — that was the point of the PR — it needs **no engine patches**: the
runtime is an out-of-tree custom module (`the_gates`) built with Godot 3's
`custom_modules=` option over pristine upstream Godot 3.6. The launcher picks
the renderer binary from the gate's `godot_version` field and the backend
serves it at `/api/download_renderer/<platform>-<version>`, so our renderer
slots in without TheGates knowing anything about this fork.

`thegates-renderer` compiles both modules into one x11 template:

```bash
scripts/build.sh thegates-renderer
```

`scripts/thegates_env.sh` assembles the sources first, pinned: the `the_gates`
module from the PR head, plus the `libzmq`/`cppzmq`/`flingfd` thirdparty the
module compiles, taken from TheGates' Godot 4.5 fork so both engines speak
the same wire protocol. libzmq needs exceptions, which Godot disables by
default, so this target also passes `disable_exceptions=no`.

What a gate needs from a project is its pack: export the project with these
templates as usual, publish the `.pck` as the gate's `resource_pack`, and the
launcher runs it on the downloaded renderer — with `physics_engine = "Box3D"`
it runs on this backend's physics. The renderer stays a plain Godot 3 binary
when no launcher IPC directory is present, so it can be smoke-tested
directly.

## Decals and blob shadows (experimental)

`decal/` is a custom module that backports the Godot 4 `Decal` node to the
GLES3 renderer, paired with `zzz_feature_decal_gles3.patch`: albedo and emission
channels with fades and a cull mask, and a growing atlas for the decal
textures. Godot 4 Compatibility semantics — the decal is not geometry, it only
modifies the fragments inside its box. GLES2 and the dummy rasterizer ignore
decals (the node still exists scene-side).

`decal/demo_advanced/` is the acceptance demo: decals over moving geometry plus
the shadow that follows a patrolling box. It now defaults to the real
`BlobShadow` node (`DECAL_DEMO_SHADOW=blob`); `=decal` keeps the old fake
projected-decal shadow for comparison and `=both` shows the two. It is
experimental; the scene shader is at its 31-conditional budget, so wiring more
channels (normal/ORM rects are reserved) needs care. See
[`docs/decal-backport-spec.md`](docs/decal-backport-spec.md).

Real blob shadows ship separately, backported from upstream PR #84804 by
`zzzzz_feature_blob_shadow_gles3.patch`: the `BlobShadow` node casts a soft
sphere or capsule shadow, `BlobFocus` tells the renderer where to prioritize
casters, and the caster `Light` gains `blob_shadow_*` parameters
(`rendering/quality/blob_shadows/*` sets the global range/gamma/intensity and
the caster budgets). Unlike the demo's projected decal, these are not geometry:
the scene shader darkens the receiving fragment. Both GLES3 and GLES2 render
them; the particles/decals/ubershader budget is unaffected because the patch
frees two conditionals instead of adding a 32nd. See
[`docs/blob-shadow-backport-spec.md`](docs/blob-shadow-backport-spec.md) for the
port notes and `test_project/blob_shadow_visual.gd` for the acceptance check.

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
| `m23_flush_spawn` … `m28_layer_asymmetry` | the Box3D bug-repro scenes (`scripts/test.sh` lists them) |
| `m29_blob_shadow_api` | `BlobShadow`/`BlobFocus` and blob-shadow `Light` params: RID lifetime, type switch, radius/offset round-trip, enable/disable |

The blob shadow feature also has a pixel test, which needs a real GL context:

```bash
xvfb-run -a -s "-screen 0 1024x600x24" \
  ../godot/bin/godot.x11.opt.tools.64 --path test_project -s blob_shadow_visual.gd
```

It renders the same frame with and without the caster and prints `BLOB_OK` when
the shadow region darkens and a control region does not (`--video-driver GLES2`
covers the GLES2 path).

## Documentation

- **This module**: this README is the setup and status reference; the module
  code comments (search for `ponytail:`) track known limitations.
- **Odisea notes**: `docs/odisea-box3d.md` — what Odisea already gains from
  the migration, the low-hanging fruit on its side (fake shadows, bakes,
  culling, queries), and the module roadmap items that matter to it.
- **Decal backport**: `docs/decal-backport-spec.md` — the Godot 4 `Decal` →
  GLES3 design, channels and limits.
- **Blob shadow backport**: `docs/blob-shadow-backport-spec.md` — the Godot 4
  `BlobShadow`/`BlobFocus` → GLES3/GLES2 port, the conditional-budget fix and
  the 3.6 interpolation adaptation.
- **FRT desktop parity / Wayland**: `docs/desktop_parity.md` — the desktop-GL
  switch, the missing platform pieces and what remains.
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
   - Optional tuning: `physics/3d/box3d_warm_starting` (default off).
     Box3D ships with warm starting disabled for determinism; enabling it
     replays each contact's previous impulses into the solver, which
     sharpens stacked bodies and long contact chains at a small step cost.

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
120-frame warmup. `scripts/bench.sh` covers the awake case only; for the
settled case see `test_project/bench/bench_settle.gd` (run it with
`godot --path test_project --no-window res://bench/bench_settle.tscn`).

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
- **The 2026-09 per-step optimizations do not move this scene.** Three runs
  on an Iris Xe laptop, before and after the event-driven dispatch,
  incremental shape edits and O(1) lookups:

  | Backend | before | after |
  |---------|--------|-------|
  | Box3D, 1 sub-step | 2.03 – 2.64 | 1.89 – 2.71 |
  | Box3D, 2 sub-steps (default) | 2.15 – 3.52 | 2.00 – 2.91 |
  | Box3D, 4 sub-steps | 3.52 – 4.13 | 2.42 – 6.04 |
  | Bullet | 2.53 – 2.98 | 2.01 – 3.17 |

  The bands overlap and Bullet's shift by as much as Box3D's: on this
  264-awake-box scene the solver dominates, the module's bookkeeping is
  noise, and the laptop's thermal throttling is the variable. The
  optimizations pay off on paths this scene does not exercise: scene loads
  with multi-shape bodies (single-shape edits are O(1) instead of a full
  rebuild), scenes with no override areas, contact monitoring, and settled
  levels.
- **A settled pile is near-free.** The 1000-box `bench_settle` scene, once its
  stacks fall asleep, costs 0.46 – 0.8 ms/frame with all contact pairs still
  registered, identical before and after the per-step changes. Sleeping
  bodies are never integrated and never dispatched to nodes, so an
  as-settled level costs the broadphase bookkeeping alone.

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
- **Per-step overhead scales with awake bodies, not with all bodies**: node
  integration dispatch (`_integrate_forces`/`_direct_state_changed`) is driven
  by the engine's per-step move events, so a settled scene costs one array
  fetch instead of an awake check per body; area space overrides gather their
  overlaps through the broadphase once per step (a scene with no override
  areas pays a single branch); contact read-back reuses one per-space buffer.

Roadmap ideas: height field grid centering, single-sided trimesh option to
halve BVH traversal, threaded stepping once determinism is verified across
runs. See `docs/odisea-box3d.md` for the Odisea-facing optimization notes.
