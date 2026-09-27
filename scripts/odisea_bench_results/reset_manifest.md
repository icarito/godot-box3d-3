# Odisea device reset manifest — `root@angel.local`

Performed 2026-09-25. Part 1 (collect stats) verified before Part 2 (reset),
and Part 2 verified before the experimental dir was removed. Nothing committed.

---

## 1. Stats collected (Part 1)

| | |
|---|---|
| Source | `root@angel.local:/storage/kilo-bench/results/**` |
| Destination | `scripts/odisea_bench_results/` (this dir) |
| Sessions | 12 (`confirm1`, `flags_n10`, `flags_n4`, `flags_n6`, `lto16`, `lto_confirm`, `main1`, `merged1`, `pilot1`, `primary1`, `sec_lto`, `sec_lto_ext`) |
| Files | 732 (290 dirs), **50,355,418 B (~51 MiB)** |
| `raw/replay_perf.json` samples | 217 |
| Verification | full-tree md5 manifest compared device vs repo: **732/732 files identical, 0 deleted** |
| Merge | rsync without `--delete`; existing 36 files preserved, 696 created |

Copied: per-session `meta.json`, `results.json`, `table.json`, `table.txt`,
`engines.txt` and every `raw/<label>/runN/{replay_perf.json,meta.json,stdout.log}`
(plus `flag.noperf` markers). **Not** copied (per instructions): the 637 MB
`backups/` dir and the engine variant binaries.

Harness/manifests already present in the repo and md5-identical to the device:
`scripts/odisea_bench_device.sh` (`5cca93e4…`), `scripts/odisea_bench_analyze.py`
(`c1246413…`), `scripts/engines*.txt` (all match).

Provenance of what each session measured is documented in
`scripts/odisea_bench_results/README.md`.

Notes:
- `sec_lto_ext` has `meta.json` + `raw/` (4×`base` + 4×`lto`) but **no**
  `results.json`/`table.*` — its aggregation was never run. Raw samples kept.
- Invalid/flagged runs: `primary1 gputimer run1`, `main1 newpin run1` (SIGSEGV,
  no perf); `primary1 mcpu run1` (SIGSEGV after perf); `flags_n6 mtune run1+run2`.

## 2. Release identity (Part 2 target)

| | |
|---|---|
| Repo / tag | `icarito/Odisea`, release `nightly` ("Nightly 2026-09-25 · 0.5.0-nightly.744+6755669") |
| Asset | `Odisea-PortMaster-0.5.0-nightly.744+6755669.zip` — 106,938,512 B |
| md5 | `5ab775acd1c675287fd10c2a085ef354` |
| sha256 | `29c8bbe384c43303b4795a8815a8fab9de38fb5cc980a69b346d26851be4092a` |
| GitHub asset digest | `sha256:29c8bbe3…be4092a` — **matches** (verified via `gh release view nightly`) |

Release contents md5:

| file | md5 | bytes |
|---|---|---|
| `Odisea.sh` | `053401c4c755b5137262b5374789090c` | 5410 |
| `odisea/odisea.pck` | `da799d135824ade0e6a1c0760efaf20e` | 208,662,032 |
| `odisea/odisea.frt.aarch64` | `a7e5346f25ca642d7123a82b36149b54` | 35,417,816 |
| `odisea/BUILD.txt` | `636ad95470998bd532c2ae8f587bb0df` | 26 (`0.5.0-nightly.744+6755669`) |
| `odisea/port.json` | `0d5d98a69ddd923c9307ae45843e0f0b` | 869 |
| `odisea/lowend.cfg` | `e60806795cabfe64e468b423cc36519b` | 2987 |
| `odisea/odisea.ini` | `4a48c3f5953fcfed437ab40258aa6954` | 641 |
| `odisea/gameinfo.xml` | `599a5534065bb01e0f6a63e750b721eb` | 635 |

The zip contains **no** `conf/`, `dev.sh`, `odisea.pck.prev`, `log.txt` or
`override.cfg`.

## 3. Pre-reset state (device, before any change)

Recorded in `pre-reset-manifest.txt` (md5 `aa5fe306e7827ddf705a3c0a37fa7e16`,
8224 B). No game process was running (`ps` showed none).

| file | pre-reset md5 |
|---|---|
| `/storage/roms/ports/Odisea.sh` | `206720539306f7d6a0e05ba0b8c77425` |
| `odisea/odisea.pck` (local `cd60d7ca`) | `2f50f664fef53f9df8bfd6b75247edd6` |
| `odisea/odisea.pck.prev` (local `7cea9461`) | `e69ba1f2f612d1a5f6664114b675383c` |
| `odisea/odisea.frt.aarch64` (local engine) | `acf0ffd13ad08222a917f9e89166fa8c` |
| `odisea/dev.sh` (user dev hook) | `9844af0c92d966784d33a55eb0b72690` |
| `odisea/BUILD.txt` (stale) | `ff5af50f0d94acfc1c8bddcc79c83c75` |
| `odisea/override.cfg` | `e60806795cabfe64e468b423cc36519b` |

`odisea/dev.sh` was preserved as `dev.sh.pre-reset` (md5 identical,
`9844af0c92d966784d33a55eb0b72690`) and copied into this repo so the user can
restore their local ANNA hook. `odisea/conf/` baseline: 30 files, content md5-set
`de74e9043dd2144226bfade9154fb9d6`, dir mtime `2026-09-19 03:08:32`.

## 4. Post-reset verification

The release zip was verified on-device (`5ab775ac…`) then extracted with
`unzip -o` over `/storage/roms/ports/` (exit 0). Removed local-only leftovers:
`odisea/odisea.pck.prev`, `odisea/dev.sh`, `odisea/log.txt`. Left in place:
`odisea/override.cfg` (launcher may regenerate), `odisea/conf/`.

| check | expected | actual | ok |
|---|---|---|---|
| `odisea/odisea.pck` | `da799d135824ade0e6a1c0760efaf20e` | `da799d135824ade0e6a1c0760efaf20e` | ✅ |
| `odisea/odisea.frt.aarch64` | `a7e5346f25ca642d7123a82b36149b54` | `a7e5346f25ca642d7123a82b36149b54` | ✅ |
| `odisea/BUILD.txt` | `0.5.0-nightly.744+6755669` | `0.5.0-nightly.744+6755669` | ✅ |
| `Odisea.sh` | `053401c4c755b5137262b5374789090c` | `053401c4c755b5137262b5374789090c` | ✅ |
| pck version string | contains `nightly.744` | `0.5.0-nightly.744+6755669` (2 hits) | ✅ |
| `odisea/conf/` | untouched | 30 files, md5-set `de74e904…`, mtime `2026-09-19 03:08:32` | ✅ |

`conf/` was checked after extraction and after the smoke test: content identical
(the only display difference was the `odisea/` path prefix between the two
manifests). The smoke test ran against a **copy** of `conf` (see below), so user
data was never written.

## 5. Smoke test (no gameplay)

Run after extraction, before deletion:

```
cd /storage/roms/ports/odisea
XDG_DATA_HOME=<copy of conf> XDG_CONFIG_HOME=<copy of conf> \
WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/var/run/0-runtime-dir \
SDL_VIDEODRIVER=wayland GODOT_SILENCE_ROOT_WARNING=1 \
timeout -s KILL 150 ./odisea.frt.aarch64 -f --video-driver GLES3 --main-pack odisea.pck --quit
```

- **Exit code: 0** (`--quit` accepted; process exited on its own, no timeout kill).
- No remaining game process afterwards.
- No `lightmap_capture_set_octree`, no `_set_user_data`, no SIGSEGV lines.
- Key log lines: `Godot Engine v3.6.4.rc.custom_build.6371881f6`,
  `OpenGL ES 3.0 Renderer: Mali-G31`,
  `[SessionManager] Early weak-hardware fast-path enabled.`,
  `[CollisionCullManager] Backend Box3D`, `[ANNAV2] Initialized`.
- Full log: `smoke-test.log` (md5 `807df03709487f8a9f5d64ba0fbeadac`).

## 6. Governor

`scaling_governor` = `ondemand` on cpu0..cpu3 (explicitly re-set and verified).

## 7. Deleted after verification

From the port (to match the published package):
`odisea/odisea.pck.prev`, `odisea/dev.sh`, `odisea/log.txt`.

Entire experimental dir `/storage/kilo-bench` (1.1 GiB): results/ (732 files),
backups/ (637 MB: local pcks + engine + dev.sh copies), all engine variant
binaries (`godot.*.arm64`), `engines*.txt`, the harness scripts, `dbg*.sh`,
`test_project/`, plus the transient `rel.zip`, `conf-smoke/`, `smoke-test.log`
and logs. The device-side originals are gone; the full results tree and the
pre-reset evidence were copied into this repo first.

- Free space before delete: `3,206,368 KiB` avail (`/dev/mmcblk1p2`, 43% used)
- Free space after delete: `4,369,084 KiB` avail (22% used)
- Freed: **1,162,716 KiB ≈ 1.11 GiB**

## 8. Could not verify / caveats

- The zip was matched against GitHub's published asset `sha256` digest, but its
  cryptographic signature (`release-2026-a.pub.pem`) was **not** verified.
- `sec_lto_ext` has no aggregated `results.json`/`table.*`; only raw samples.
- The 637 MB `backups/` and engine variant binaries were intentionally **not**
  copied to the repo (per instructions) and are now deleted from the device;
  only their md5s are recorded here and in `README.md`.
- Pre-reset `Odisea.sh` content was recorded by md5 in the manifest but the old
  file itself was not archived (overwritten by the release launcher).
- `odisea/override.cfg` is intentionally left in place (not part of the release;
  the launcher may regenerate it).
