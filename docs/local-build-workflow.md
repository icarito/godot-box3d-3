# Local build workflow: engine trees, scons cache, consumers

Date: 2026-09-27. Why local rebuilds exploded, and the setup that stops it.

## The problem

`scripts/build.sh` resets the Godot checkout (`git checkout -- .`) and re-applies every
patch in `patches/` (and `patches/frt/` on `platform/frt`) before each build. Scons decides
by content (`Decider("MD5-timestamp")`), so re-applying the *same* patch set rebuilds
nothing. But:

- **Branches carry different patch sets.** Switching the fork's branch and building
  applies a different set; the big ones touch many files (`zzzzz_feature_blob_shadow_gles3`
  147, `zzzzz_feature_glow_map_gles3` 57, `zzz_feature_decal_gles3` 49), several of them
  renderer headers most of the engine includes → near-full rebuild on every switch.
- **One Godot tree was shared by every consumer.** Odisea's `tools/godot_bin.sh` rebuilds
  the x11 editor whenever a file under `patches/`, `box3d/` or `scripts/build.sh` is newer
  than the binary (a branch switch is enough), other branches build there too, and gdtk
  built FRT with extra modules in the same tree. Each one leaves the tree in its state and
  the next pays the rebuild.
- **Generated module headers are per tree, not per platform.** `modules/modules_enabled.gen.h`
  and `modules/register_module_types.gen.cpp` are written at configure time for the
  platform/modules being built and shared by every platform in that tree. After an x11
  build, an FRT build in the same tree (or in a copy of it, `.sconsign.dblite` included) can
  keep the x11 set: symptom `undefined reference to register_denoise_types()` at link time.
  Fix: `rm modules/modules_enabled.gen.h modules/register_module_types.gen.*` and rebuild.

## The setup

1. **Scons object cache (on by default).** `scripts/build.sh` exports
   `SCONS_CACHE=~/.cache/scons-godot3` (`SCONS_CACHE_LIMIT`, MB, default 30000) unless the
   caller set it; `SCONS_CACHE=""` disables it. Godot's `SConstruct` turns it into
   `CacheDir`, so objects are keyed by content + flags: going back to a branch or flag set
   already built retrieves objects instead of compiling them. Shared across trees. It only
   holds what was built after enabling it — the gain shows from the second visit.

2. **One engine tree per consumer.** `scripts/build.sh` honours `GODOT_DIR` (default:
   `../godot`). Keep separate trees for consumers with different module/patch sets, as git
   worktrees of the same Godot clone:

   ```sh
   git -C ../godot worktree add --detach ../godot-dev <pinned-commit>
   GODOT_DIR=../godot-dev scripts/build.sh frt-editor
   ```

   Current layout on the dev box:

   | Tree | Used by | Notes |
   |---|---|---|
   | `godot` | this fork's branches, Odisea's local editor builds (`tools/godot_bin.sh` with `ODISEA_ENGINE=fork`) | `scripts/build.sh` default |
   | `godot-dev` | gdtk (`modules/imgui`, `modules/wayland`) | FRT build with gdtk's custom modules, `extra_suffix=gdtk` |

   A new tree starts with a rebuild of what its first configure regenerates; afterwards
   it is incremental. Copying a tree (objects included) is faster than a first build, but
   see the generated-headers note above.

3. **Consumers take released binaries by default.** CI publishes the binaries on every
   `v*` tag; Odisea pins one in `.github/box3d_release` and `tools/godot_bin.sh` downloads
   it (to `~/.cache/odisea-godot/<tag>/`). Building the editor from the local fork is
   opt-in (`ODISEA_ENGINE=fork`), for engine work that is not released yet.

## FRT keyboard: physical scancodes

`patches/frt/zzzzz_sdl_physical_scancode.patch`: FRT built `InputEventKey` only from the SDL
*keysym* (layout-dependent; its table has no `SDLK_MINUS` and no non-US punctuation) and
copied it into `physical_scancode` (`// TODO`). Now:

- `physical_scancode` comes from the SDL *scancode* through a positional (US) table
  (`map_key_sdl2_scancode`); the logical `scancode` is unchanged, so games see no difference.
- Keys with no Godot 3 constant use a convention consumers must honour:
  `RALT` (AltGr) → `KEY_HYPER_R`, `NONUSBACKSLASH` (ISO `<>` key) → `KEY_HYPER_L`.
- With Ctrl or Super held, key events are emitted right away (unicode 0) instead of waiting
  for an `SDL_TEXTINPUT` that never comes (Ctrl+C used to be lost). AltGr still waits for the
  text event (it produces `@`, `#`, ...).

gdtk's Wayland compositor maps `physical_scancode` to evdev codes; verified with XTest on an
`es` layout (`a-b_c@d`, Ctrl+C).
