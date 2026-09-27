# Odisea ARM64 device bench — collected results & provenance

Byte-identical copy of the device bench results tree
`root@angel.local:/storage/kilo-bench/results/`, collected 2026-09-25.

- 12 session dirs, 732 files, 50,355,418 bytes (~51 MiB) — **all 732 files
  verified identical by md5** against the device.
- Data/results only. The 637 MB `/storage/kilo-bench/backups/` dir and the
  engine variant binaries were intentionally **not** copied.
- Aggregates committed as-is: each session's `meta.json`, `results.json`,
  `table.json`, `table.txt` and `engines.txt`.
- The per-run samples (`raw/<label>/runN/*.json`: the 217 `replay_perf.json`
  plus the JSON tables, 442 files) are archived losslessly in
  `raw-samples.tar.gz` (5.7 MiB) to keep the repo light. Inspect with
  `tar xzf raw-samples.tar.gz`.

## Device facts

| | |
|---|---|
| Host | `angel` = Anbernic RG351V |
| SoC | Rockchip RK3326, 4× ARM Cortex-A35, aarch64 (fp asimd crc32; no LSE) |
| RAM | 981 MB, no swap |
| OS | ROCKNIX `20260901` (BUILD_ID `1ebff24f36501fb6493beb2bf83bf2604536d9aa`, HW_DEVICE=RK3326) |
| Kernel | 7.1.2 aarch64 |
| Governor | `ondemand`; pinned to `performance` during runs, restored to `ondemand` after |

## Harness

- `../odisea_bench_device.sh` (entry) + `../odisea_bench_analyze.py`; repo md5
  identical to device (`5cca93e4…` / `c1246413…`).
- Engine manifests `../engines*.txt` (all md5 identical to device).
- Run mode: `ODISEA_REPLAY_PERF=1` + `--replay replay_1790167671.json`; output
  `user://replay_perf.json` (XDG_DATA_HOME=conf/). Round-robin per rep with the
  start offset rotated each round, per-run soc/gpu temp and stdout captured.

## Workload contexts (pck)

| Context | pck | md5 | frames | DRIFT_CHECK | role |
|---|---|---|---|---|---|
| PRIMARY | `odisea.pck.prev` | `e69ba1f2f612d1a5f6664114b675383c` | 1352 | **0.000314** | replay-consistent (LOCAL build of `7cea9461`) |
| SECONDARY | `odisea.pck` | `2f50f664fef53f9df8bfd6b75247edd6` | 1352 | **18.075** | drifted (LOCAL build of `cd60d7ca`); see caveats |

- Shipped engine at bench time (label `baseline`):
  `acf0ffd13ad08222a917f9e89166fa8c` (== `backups/odisea.frt.aarch64.shipped.WS-A`).
- Replay `replay_1790167671.json` md5 `72d5f630633badad4796145793b4f969`.
- `override.cfg` == `lowend.cfg` md5 `e60806795cabfe64e468b423cc36519b`.

## Engine variant binaries (md5, device `/storage/kilo-bench/`)

| label | file | md5 |
|---|---|---|
| baseline | `odisea.frt.aarch64` (port, shipped) | `acf0ffd13ad08222a917f9e89166fa8c` |
| oldpin | `godot.oldpin.arm64` | `c911ab4c0c46f011b41c56d03f5ca67a` |
| — | `odisea.frt.aarch64` (bench dir copy) | `c911ab4c0c46f011b41c56d03f5ca67a` (== oldpin) |
| mcpu | `godot.mcpu.arm64` | `30b0cdfcf675b842cebeff149ee738be` |
| gputimer | `godot.gputimer.arm64` | `b4be2e96c7d98edddcb3237d7c4715ef` |
| newpin | `godot.newpin.arm64` | `a53b2decd05d527ef3fa64288fe738a6` |
| nightly13 | `godot.nightly13.arm64` | `b55bfd71e4bda8a9874c86f71e97a7fe` |
| nightly14 | `godot.nightly14.arm64` | `8d8d14dc22999a703cf6e99a25091736` |
| base | `godot.base.arm64` | `52df97fd0f96d7863ed60ec5ce1c07db` |
| mtune | `godot.mtune.a35.arm64` | `dbeaf83ff51d297ef2045e55f711e295` |
| lto | `godot.lto.arm64` | `84a1df051921b2df3dc1b06841054ece` |

## Sessions

| session | pck | reps | labels | aggregation |
|---|---|---|---|---|
| pilot1 | current | 1 | baseline | results/table present (smoke) |
| primary1 | prev | 3 | baseline, oldpin, mcpu, gputimer, newpin, nightly13, nightly14 | present |
| main1 | current | 3 | baseline, oldpin, mcpu, gputimer, newpin, nightly13, nightly14 | present |
| confirm1 | prev | 5 | baseline, nightly13 | present |
| merged1 | prev | 8 (primary1 3 + confirm1 5) | baseline, nightly13 | present |
| flags_n6 | prev | 6 | baseline, base, mtune, lto | present |
| flags_n4 | prev | 4 | baseline, base, mtune, lto | present |
| flags_n10 | prev | 10 | baseline, base, mtune, lto | present |
| lto_confirm | prev | 6 | base, lto | present |
| lto16 | prev | 16 (flags_n10 + lto_confirm) | base, lto | present (merged view) |
| sec_lto | current | 8 | baseline, base, lto | present |
| sec_lto_ext | current | 5 req / 4+4 done | base, lto | **missing**: only `meta.json` + `raw/`; no `results.json`/`table.*` (aggregation not run) |

Invalid runs (no valid `replay_perf.json`): `primary1 gputimer run1`,
`main1 newpin run1` (SIGSEGV, no perf); `primary1 mcpu run1` (SIGSEGV after perf
written — counted, flagged). `flags_n6 mtune run1+run2` early-startup SIGSEGV.
No OOM kills observed during bench runs.

## Noise floor

- Unpaired repeat spread, `primary1` baseline n=3:
  fps 0.17 | ms_process mean 8.3 | ms_process p50 1.5 | p95 139 | ms_physics mean 1.7.
- Unpaired repeat spread, `merged1` baseline n=8:
  fps 0.95 | ms_process mean 9.0 | p50 2.5 | p95 147 | ms_physics mean 2.9.
- Unpaired repeat spread, `flags_n10` baseline n=10:
  fps 1.28 | ms_process mean 11.2 | p50 4.05 | p95 211 | ms_physics mean 3.78.
- Paired noise proxy (SD of base-vs-baseline control deltas, `flags_n10`):
  fps 0.513 | ms_process mean 5.32 | p50 4.02 | ms_physics mean 0.892.
  p95 is spike-dominated and unusable as a noise metric.

## Known caveats

1. **PRIMARY context is `odisea.pck.prev` (`7cea9461`)** — the only
   replay-consistent context: DRIFT_CHECK 0.000314, 1352 frames.
2. **The current local `odisea.pck` (`cd60d7ca`) drifts DRIFT 18.075** and its
   replay run is **invalid**: the player leaves the floor, so the recorded end
   state is not reproduced. It was kept as a secondary context only. It also
   carries a broken empty lightmap (`RingHub.lmbake`, 332 B) that emits
   `lightmap_capture_set_octree` / `_set_user_data` errors and causes
   intermittent SIGSEGV (`lightmap_capture_set_octree`).
3. Any pck.prev gain must be re-checked on the current pck; none exists.
4. `sec_lto_ext` raw samples exist but were never aggregated; treat them as
   unprocessed.
5. Sep-23 456-frame baseline is superseded (engine and pck changed 2026-09-24):
   compare only within the same pck context.
