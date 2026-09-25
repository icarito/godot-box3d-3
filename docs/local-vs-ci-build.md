# Local vs CI Odisea build — root-cause investigation

Date: 2026-09-25. Investigated by the `@general` subagent (device left to WS-A, which was
running `sec_lto` during this work; no device benchmark was run here).

## TL;DR

1. **The premise is wrong: neither pck on the device is a CI build.** Both are local
   `make portmaster` / `godot --export-pack` outputs, from two different commits. The CI
   release (nightly.744, commit `67556690`) is *not* on the device — its pck was
   overwritten after install.
2. **The "2x" is not a real per-frame regression.** On the same pinned replay
   (`replay_1790167671.json`, 1352 frames) the two pcks have essentially identical
   steady-state cost: `ms_process` median 37.0 vs 37.3 ms, fps 10.47 vs 10.26, GDScript
   tick 9.38 vs 9.42 ms, physics 19.1 vs 17.6 ms. The `ms_process` **mean** differs
   (51 → 67 ms) only because of a path-dependent low-fps plateau (≈331 ms/frame vs
   ≈641 ms/frame, i.e. the ≈2x) that each run hits in a **different region**, because the
   pinned replay no longer reproduces on the newer build (`DRIFT_CHECK` 0.0003 vs 18.07).
3. **Two real, separate defects were found** (both fixable):
   - the current local pck contains the **pre-fix broken `BakedLightmap`** (332-byte
     `RingHub.lmbake`, empty octree) from `cd60d7ca`, predating the fix in `a0d26c44`;
     it spams `lightmap_capture_set_octree` errors and matches the observed SIGSEGV;
   - the local packaging pipeline ships **~112 MB of unused texture variants**
     (ETC + S3TC) that CI strips for `linux_arm64`; local pck is 316 MB vs CI 208 MB.

## 1. Provenance

Method: the export preset uses `script_export_mode=0` (text), so `.gd`/`.tscn` are stored
byte-identical to their source blobs. Every `.gd` in each pck was matched (MD5 of content)
against the repo tree of candidate commits (`docs` check reproducible with
`/tmp/kilo/pck_tool.py` + the script in this investigation).

| artifact | path | md5 | mtime | embedded `config/version` | pipeline | exact source |
|---|---|---|---|---|---|---|
| "current" | `/storage/roms/ports/odisea/odisea.pck` | `2f50f664…` | 2026-09-24 19:46:14 | `v0.5.0` (plain) | **local** `--export-pack` | **`cd60d7ca`** (719/719 `.gd` match 100%) |
| "prev" | `odisea.pck.prev` | `e69ba1f2…` | 2026-09-23 20:56:11 | `v0.4.0` (plain) | **local** `--export-pack` | **`7cea9461`** (714/714 `.gd` match 100%) |
| CI release (fetched) | nightly ARM64 `Odisea-Tech-Demo-Linux-ARM64-0.5.0-nightly.744+6755669.pck.gz` | `da799d13…` (inflated) | — | `0.5.0-nightly.744+6755669 (2026-09-25)` + `build_meta.json` | **CI** `export_platform.yml` | `67556690` (`build_meta.json.commit`) |

Discriminators used and validated:

- **Version string.** CI injects the run version into `project.godot` before exporting
  (`export_platform.yml:163-166`) and always writes `build_meta.json`
  (`:171-200`). The fetched CI pck contains both
  (`application/config/version = "0.5.0-nightly.744+6755669 (2026-09-25)"` and
  `build_meta.json`). **Neither device pck contains a `nightly` string or
  `build_meta.json`** → both are `make portmaster` outputs.
- **Build latency.** `current` mtime is 2 min after commit `cd60d7ca` (19:44:43 → 19:46:14);
  `prev` mtime 56 min after `7cea9461`. A CI run cannot produce a tagged pck in 2 min.
- **Commit fingerprint.** All `RingHub_Level.tscn`/`.gd` bytes match exactly one commit
  each (see table). `prev` is built from `7cea9461`; the later `bd5f9d7c` (20:55) changed
  `PlayerControllerV2.gd` etc., and those files are *absent* from `prev` — consistent.
- The PortMaster install metadata (`BUILD.txt = 0.4.0-nightly.736+5bddfd5`) describes the
  last **CI nightly.736** package installed; its pck has since been overwritten by the
  two local builds. `nightly.736` is 8 nightlies behind the current CI head (744).

All pcks report engine `3.6.4` in the PCK header (same export engine family).

## 2. Is the "2x" real? (separating build from drift)

Reused the existing harness results (`/storage/kilo-bench/results/{primary1,main1}`,
same pinned replay, 3 runs each, `governor=performance`). Pooled steady state (`frame>90`):

| metric | prev `7cea9461` (primary1) | current `cd60d7ca` (main1) | verdict |
|---|---|---|---|
| frames (same replay) | 1352 | 1352 | same input |
| `DRIFT_CHECK` | 0.000314 | 18.075035 | **workload differs** |
| `ms_process` median | 37.0 | 37.3 | identical (+0.3, noise ±1.5) |
| `ms_process` p90 | 41.4 | 44.0 | +2.6 ms |
| `ms_process` mean | 51.1 | 67.3 | +16 ms, driven by tail |
| `ms_physics` mean | 19.1 | 17.6 | current *lighter* |
| fps mean | 10.47 | 10.26 | −2 %, within spread |
| median draw calls | 64 | 51 | current *fewer* |
| GDScript tick ms | 9.38 | 9.42 | identical |
| `SM.player_step` ms | 4.11 | 4.03 | identical |
| `SessionManager` ms | 5.95 | 6.06 | identical |

The mean gap is **not** distributed cost; it is a handful of contiguous low-fps plateaus.
`ms_process` is constant for dozens of frames during them while the player position keeps
advancing (a genuinely slow region, ~1.5–3 fps), and they occur at **different places**:

- `prev`: frames ≈141–175 at `ms_process=331.3` (~3.0 fps), position ≈(6.5, 4.7, 2.0).
- `current`: frames ≈610–676 at `ms_process=640.7` (~1.56 fps), position ≈(−3, 0, −20).

Those two regions are ~1.9–2x apart in per-frame cost — that is where the "2x" number
comes from. Because the replay drifts on the current build, the two runs never visit the
same region, so the comparison is confounded. On robust metrics (median, p90, fps, script
time) there is **no genuine slowdown**; the current build is even lighter in physics and
draw calls. The reporting harness's own noise floor for `ms_process` **mean** is ±8.3 ms
(prev, n=3) and ±23.3 ms (current, n=3), and for p95 ±139/±108 ms — the 16 ms mean delta
does not clear the noise floor as evidence of a build-wide regression.

## 3. Content / configuration diff

**Code/content (from the exact-commit match + pck listing):** current = `7cea9461` +
Sep-24 RingHub work up to `cd60d7ca`; prev = `7cea9461`. The file list gained 14 files,
all RingHub lightmap/PBR: `RingHub.lmbake`, `RingHub_BakeLights.tscn`,
`RingHubLightState.gd`, `RingHubFloorMaterial.gd`, `RingHub_Floor_*` meshes/materials,
`RingHub_IndustrialRailing.tscn`, `RingHub_ScaffoldSource.tscn`, and 3 `tools/` bake
scripts. Net unpacked bytes +189,777 (+0.06 %).

`RingHub_Level.tscn` diff (extracted from both pcks):

- adds a `BakedLightmap` node, `extents = Vector3(36,19,36)`, `layers=1023`,
  `light_data = res://core_v2/levels/RingHub.lmbake`;
- sets `use_in_baked_light = true` on many scaffold/dome/floor meshes;
- adds 1 `OmniLight` (shadow off) + a pedestal button + `RingHubLightState.gd`
  (deterministic DARK/LIT state machine, also drives lightmap energy);
- `RingHubBakeLights` is an `instance_placeholder` (bake-time rig, not 22 runtime lights).

**Broken lightmap data.** The current pck's `RingHub.lmbake` is **332 bytes** — a
`BakedLightmapData` resource with metadata only, no `octree`/`user_data` payload (verified
by hexdump: the `octree`/`user_data` properties are empty). That is exactly the
`cd60d7ca` ("RingHub listo para bake") placeholder. `a0d26c44` replaced it with 3569 bytes
plus 23 lightmaps, and the fetched CI pck ships **3569 bytes**. At runtime the current
build logs:

```
ERROR: Condition "p_octree.size() == 0 || (p_octree.size() % sizeof(LightmapCaptureOctree)) != 0" is true.
   at: lightmap_capture_set_octree (drivers/gles3/rasterizer_storage_gles3.cpp:6324)
ERROR: Condition "p_data.size() <= 0" is true.
   at: _set_user_data (scene/3d/baked_lightmap.cpp:154)
```

WS-A also saw a SIGSEGV (`rc=139`) inside `lightmap_capture_set_octree` on this pck
(flaky), and the device `log.txt` shows an OOM `Killed` on the current pck.

**Build configuration / packaging (the real local-vs-CI difference).** The local export
bundles compressed texture variants that the Mali-G31 never samples:

| format | local current | CI `linux_arm64` |
|---|---|---|
| `.etc2.stex` | 48.7 MB (68) | 48.4 MB (68) |
| `.etc.stex` | 46.6 MB (68) | — |
| `.s3tc.stex` | 65.8 MB (68) | — |
| total pck | **316.2 MB** (2918 files) | **208.4 MB** (2826 files) |

CI runs "Limit texture import formats" (`export_platform.yml:690-745`) which, for
`linux_arm64`, writes `vram_compression/import_s3tc=false`, `import_bptc=false`,
`import_etc=false`, `import_etc2=true`, `import_pvrtc=false` into `project.godot` before
import. The checked-in `project.godot` (`[rendering]`) instead has
`import_bptc=true`, `import_etc=true`, `import_pvrtc=true` (s3tc/etc2 default true), so a
local `make portmaster` import generates/exports ETC and S3TC as well. Extra dead weight =
**~112 MB** (ETC 46.6 + S3TC 65.8), matching the 316.2 − 208.4 = 107.8 MB delta. CI's own
comment measured this: "en linux_arm64 eran 100 MB de .stex que el handheld nunca abre".

Note: the two local pcks differ from each other by only 0.06 %, which is why the size clue
looked absent — the 112 MB gap is only visible against the CI artifact.

## 4. Root-cause candidates (ranked)

1. **Drift confound, not a build regression** (high confidence). The pinned replay only
   reproduces on `7cea9461`; on `cd60d7ca` it drifts (`DRIFT_CHECK` 18.07), so the runs
   visit different regions and the mean/peak diff is a workload artifact. The "2x" is the
   peak plateau ratio (640.7 vs 331.3 ms/frame ≈ 1.9x) between different areas. Evidence:
   identical medians/p90/fps/script time; current physics and draw calls lower.
2. **Stale local build carrying the broken pre-fix lightmap** (high confidence). Current
   pck = `cd60d7ca` (332-byte empty `.lmbake`); the fix `a0d26c44` (3569 bytes) and the CI
   pck (3569 bytes) are newer. Causes runtime GI errors and the `lightmap_capture_set_octree`
   crash; may also contribute hitch spikes. Not a per-frame 2x, but a real correctness/stability defect.
3. **Packaging config gap: 112 MB of unused ETC/S3TC variants** (high confidence, measured).
   Local export lacks CI's per-platform texture-format limiting. Real size/load/install
   cost; does not change steady-state fps because the engine still picks ETC2 on Mali-G31.
4. **Premise misattribution** (high confidence). The "CI release" was not present on the
   device during the A/B; both artefacts measured were local builds of different commits.
   The installed CI metadata (`nightly.736`) is stale.

## 5. Recommended fix / decisive next measurement

Fixes (in order):

1. **Rebuild the device pck from ≥ `a0d26c44` (or HEAD `67556690`)** so the baked lightmap
   data is valid; this removes the GI errors and the `lightmap_capture_set_octree` crash.
2. **Adopt CI's per-platform texture-format limiting in the local pipeline** (or add the
   `vram_compression/import_*` keys to the checked-in `project.godot` for the arm64/PortMaster
   path) to reclaim ~112 MB and speed install/load.
3. Re-record the pinned replay at the new build (or drop it) since Sep-24 content changes
   made `replay_1790167671` non-reproducing.

Decisive next measurement (isolates build/packaging from content and drift):

- Produce a **local `make portmaster` pck of `67556690`** (same commit as the CI artifact
  already downloaded here) and compare it against the **CI pck of `67556690`** on a workload
  that is drift-free for that content — either a replay recorded at `67556690`, or a fixed
  scene/camera with streaming halted. Report `ms_process` median/p90, fps, RSS, pck load
  time and pck size. Because both sides then share identical content, any residual delta is
  a true local-vs-CI build/packaging difference; expect the size gap (#3) to reproduce and
  the per-frame cost to be within noise.

## Reproducibility / evidence artifacts

- PCK tool + extracted lists: `/tmp/kilo/pck_tool.py`, `/tmp/kilo/{current,prev,ci}.list`.
- Extracted scenes/scripts: `/tmp/kilo/{current,prev}.RingHub_Level.tscn`,
  `/tmp/kilo/current.RingHub.lmbake`, `/tmp/kilo/prev.{PlayerControllerV2,HudModeOverlay}.gd`.
- CI pck: `/tmp/kilo/ci.pck.gz` → `/tmp/kilo/ci.pck` (md5 `da799d13…`), from
  `gh release download nightly -R icarito/Odisea`.
- Device copies are byte-identical to the live files (md5 re-verified: `2f50f664…`,
  `e69ba1f2…`); nothing on the device was modified.
