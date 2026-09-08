# godot-box3d-3

**Box3D** is a Godot 3.6.x custom module that integrates upstream
[Bullet Physics](https://github.com/bulletphysics/bullet3) (tag `3.25`, the
current release) as an alternative 3D physics server, without forking Godot.

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
    ├── box3d_physics_server.*    # Box3DPhysicsServer: PhysicsServer implementation
    └── thirdparty/box3d/         # git submodule: bullet3 @ 3.25
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

## Status

First-commit skeleton:

- `Box3DPhysicsServer` implements the complete Godot 3.6 `PhysicsServer`
  interface (182 pure virtual methods) as a compile-ready no-op stub.
- Bullet 3.25 is vendored via git submodule and fully compiled into the module
  (LinearMath, BulletCollision, BulletDynamics, BulletSoftBody), mirroring the
  proven file list and defines (`BT_THREADSAFE`, `BT_USE_OLD_DAMPING_METHOD`)
  used by Godot 3.6's own bullet module.

Roadmap: wire `Box3DPhysicsServer` to a real `btDiscreteDynamicsWorld`
(space/shape/body lifecycle first), reusing Godot's bullet module
(`godot/modules/bullet`) as the reference implementation against upstream 3.25.
