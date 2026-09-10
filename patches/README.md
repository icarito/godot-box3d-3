# Engine patches

Fixes to Godot itself that the built binaries carry. They are applied by
`scripts/build.sh` before compiling, so every binary this repo publishes has
them. Each is upstreamable and none touches the Box3D module.

| Patch | Fixes |
|-------|-------|
| `scene_glsl_directional_ubo.patch` | `DirectionalLightData` ends in a `mediump vec3 pad`, which std140 aligns differently than the C++ struct expects. Strict GLES3 implementations (WebGL 2) then read the directional light data shifted. Three scalar floats pad the block the way std140 actually lays it out. |
| `dir_access_jandroid_make_dir_recursive_idempotent.patch` | `DirAccessJAndroid::make_dir_recursive()` returned `ERR_ALREADY_EXISTS` for a directory that exists, unlike the generic `DirAccess` it overrides. That disabled `ShaderCacheGLES3` on every Android run after the first. |
| `gles3_async_compile_queue_fallback.patch` | Asynchronous shader compilation via the secondary context (`shader_compilation_mode` 1 and 2) had no way out of a failure. The queue thread reported a failed build as *in progress*, so the variant stayed in `COMPILE_STATUS_PROCESSING_AT_QUEUE` forever, never freed its compile slot, and `INFO_SHADER_COMPILES_IN_FRAME` never returned to zero; a binary the main context refused at link time was parked in `COMPILE_STATUS_ERROR` for the session. Both now fall back to compiling that variant synchronously in the main context, which is what Adreno needs for the ubershader (*Sampler location or component exceeds max allowed*). `ShaderCacheGLES3::store()` also writes to a temporary and renames, so a run interrupted mid-write no longer leaves the empty file the next run reports as corrupted. |
| `gles3_diag_shader_link_and_cache.patch` | **Temporary.** Diagnostics, not a fix: names the cache entry and its header bytes when `retrieve()` calls one corrupted, says whether a failed link was the ubershader and what the driver's sampler limits are, and names the `program_binary.source` a rejected program came from. Delete this file once the Adreno investigation closes. |

Origin: the first two come from the Odisea game project
(`src/tools/godot_web_template/`, `src/tools/godot_android_template/`), which
carried them locally against Godot 3.6.
