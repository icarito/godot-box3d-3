# Engine patches

Fixes to Godot itself that the built binaries carry. They are applied by
`scripts/build.sh` before compiling, so every binary this repo publishes has
them. Each is upstreamable and none touches the Box3D module.

| Patch | Fixes |
|-------|-------|
| `scene_glsl_directional_ubo.patch` | `DirectionalLightData` ends in a `mediump vec3 pad`, which std140 aligns differently than the C++ struct expects. Strict GLES3 implementations (WebGL 2) then read the directional light data shifted. Three scalar floats pad the block the way std140 actually lays it out. |
| `dir_access_jandroid_make_dir_recursive_idempotent.patch` | `DirAccessJAndroid::make_dir_recursive()` returned `ERR_ALREADY_EXISTS` for a directory that exists, unlike the generic `DirAccess` it overrides. That disabled `ShaderCacheGLES3` on every Android run after the first. |

Origin: both come from the Odisea game project
(`src/tools/godot_web_template/`, `src/tools/godot_android_template/`), which
carried them locally against Godot 3.6.
