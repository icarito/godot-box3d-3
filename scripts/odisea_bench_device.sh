#!/bin/bash
# Odisea device benchmark harness (WS-A).
# Runs engine binaries against the shipped pck + replay on root@angel.local,
# archives each raw replay_perf.json, and emits a comparable table (text + JSON).
#
# Entry point:
#   scripts/odisea_bench_device.sh [--runs N] [--timeout S] [--only LABEL]
#                                  [--engines FILE] [--session NAME] [--cooldown C]
#
# Defaults: runs=3, timeout=180s, cooldown target 55 C, engines=/storage/kilo-bench/engines.txt
# Never touches the shipped engine/pck/override.cfg; only reads them. Restores governor on exit.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PORT_DIR="${PORT_DIR:-/storage/roms/ports/odisea}"
BENCH_DIR="${BENCH_DIR:-/storage/kilo-bench}"
RESULT_ROOT="${RESULT_ROOT:-$BENCH_DIR/results}"
REPLAY="${REPLAY:-replay_1790167671.json}"
ENGINES_FILE="$BENCH_DIR/engines.txt"
RUNS=3
TIMEOUT=240
COOLDOWN_C=55
COOLDOWN_MAX=45
ONLY=""
SESSION=""
PCK="${PCK:-$PORT_DIR/odisea.pck}"

usage() { sed -n '2,12p' "$0"; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --runs) RUNS="$2"; shift 2;;
    --timeout) TIMEOUT="$2"; shift 2;;
    --only) ONLY="$2"; shift 2;;
    --engines) ENGINES_FILE="$2"; shift 2;;
    --session) SESSION="$2"; shift 2;;
    --cooldown) COOLDOWN_C="$2"; shift 2;;
    --pck) PCK="$2"; case "$PCK" in /*) ;; *) PCK="$PORT_DIR/$PCK";; esac; shift 2;;
    -h|--help) usage;;
    *) echo "unknown arg: $1" >&2; usage;;
  esac
done

[ -d "$PORT_DIR" ] || { echo "missing PORT_DIR $PORT_DIR" >&2; exit 2; }
[ -f "$ENGINES_FILE" ] || { echo "missing engines file $ENGINES_FILE" >&2; exit 2; }
[ -f "$PCK" ] || { echo "main-pack not found: $PCK" >&2; exit 2; }

[ -n "$SESSION" ] || SESSION="$(date +%Y%m%d-%H%M%S)"
SESSION_DIR="$RESULT_ROOT/$SESSION"
RAW_DIR="$SESSION_DIR/raw"
mkdir -p "$RAW_DIR"

CONFDIR="$PORT_DIR/conf"
USERDIR="$CONFDIR/godot/app_userdata/Odisea"
[ -d "$USERDIR" ] || { echo "user:// dir not found: $USERDIR" >&2; exit 2; }
[ -f "$USERDIR/$REPLAY" ] || { echo "replay not found: $USERDIR/$REPLAY" >&2; exit 2; }

GOV_PATHS=(/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor)
SOC_TEMP=/sys/class/thermal/thermal_zone0/temp   # soc-thermal (RK3326)
GPU_TEMP=/sys/class/thermal/thermal_zone1/temp   # gpu-thermal

# ---------- helpers ----------
now_ms() { date +%s%3N; }
get_gov() { cat "${GOV_PATHS[0]}" 2>/dev/null || echo "?"; }
set_gov() { local g="$1"; local p; for p in "${GOV_PATHS[@]}"; do echo "$g" > "$p" 2>/dev/null || true; done; }
read_temp() { local f="$1"; if [ -r "$f" ]; then local t; read -r t < "$f"; echo $((t/1000)); else echo -1; fi; }
cur_freqs() { local p out=""; for p in /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq; do out="$out$(cat "$p" 2>/dev/null),"; done; echo "$out"; }
md5of() { md5sum "$1" 2>/dev/null | cut -d' ' -f1; }

# Kill only our engines, matched by resolved exe (never by cmdline).
kill_engines() {
  local p exe
  for p in /proc/[0-9]*; do
    exe="$(readlink "$p/exe" 2>/dev/null)" || continue
    case "$exe" in
      */odisea.frt.aarch64|/storage/kilo-bench/*.arm64)
        kill -9 "${p##*/}" 2>/dev/null || true;;
    esac
  done
  sleep 1
}

# ---------- backup dev.sh (never modified; restore only if changed) ----------
DEVSH="$PORT_DIR/dev.sh"
DEVSH_BAK="$BENCH_DIR/backups/dev.sh.WS-A.$SESSION"
mkdir -p "$(dirname "$DEVSH_BAK")"
DEVSH_MD5_BEFORE=""
if [ -f "$DEVSH" ]; then
  cp -f "$DEVSH" "$DEVSH_BAK"
  DEVSH_MD5_BEFORE="$(md5of "$DEVSH")"
fi

# ---------- preconditions snapshot ----------
echo "=== Odisea bench session $SESSION ==="
echo "device: $(uname -sm) $(cat /etc/os-release 2>/dev/null | grep -m1 OS_VERSION)"
echo "governor before: $(get_gov)  freqs: $(cur_freqs)"
echo "temp before (soc/gpu C): $(read_temp $SOC_TEMP) / $(read_temp $GPU_TEMP)"
echo "replay: $USERDIR/$REPLAY  md5: $(md5of "$USERDIR/$REPLAY")"
echo "main-pack: $PCK  md5: $(md5of "$PCK")"
echo "engines file: $ENGINES_FILE"
echo "session dir: $SESSION_DIR"
echo

# ---------- governor: pin performance, restore on exit ----------
GOV_ORIG="$(get_gov)"
restore_gov() {
  set_gov "$GOV_ORIG"
  echo "[governor restored to $GOV_ORIG] restore manually with: echo $GOV_ORIG > /sys/devices/system/cpu/cpuN/cpufreq/scaling_governor (N=0..3)"
}
trap restore_gov EXIT
set_gov performance
sleep 1
echo "governor pinned: $(get_gov)  freqs: $(cur_freqs)"
echo

# ---------- run env (mirrors Odisea.sh / mod_ROCKNIX + dev.sh replay hook) ----------
export XDG_CONFIG_HOME="$CONFDIR"
export XDG_DATA_HOME="$CONFDIR"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-1}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run/0-runtime-dir}"
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-wayland}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-pulseaudio}"
export MALI_DEFAULT_DISPLAY="${MALI_DEFAULT_DISPLAY:-wayland}"
export SDL_GAMECONTROLLERCONFIG_FILE="${SDL_GAMECONTROLLERCONFIG_FILE:-/storage/.config/SDL-GameControllerDB/gamecontrollerdb.txt}"
export LD_LIBRARY_PATH="/usr/lib:${LD_LIBRARY_PATH:-}"
export ODISEA_REPLAY_PERF=1
export ODISEA_EARLY_WEAK_HARDWARE=1
export ODISEA_UNSHADED="${ODISEA_UNSHADED:-3}"
export ODISEA_UPDATES_MANAGED_BY=PortMaster
export FRT_NO_EXIT_SHORTCUTS=FRT_NO_EXIT_SHORTCUTS
export GODOT_SILENCE_ROOT_WARNING=1

RES_ARGS="-f"   # launcher also passes --resolution 640x480; -f alone is proven stable here

run_one() {
  local label="$1" binpath="$2" idx="$3"
  local rdir="$RAW_DIR/$label/run$idx"
  mkdir -p "$rdir"
  kill_engines
  rm -f "$USERDIR/replay_perf.json"

  local perf="$USERDIR/replay_perf.json" have=0

  local t0 g0 f0 seek soc0 gpu0
  t0=$(date +%s); seek=$t0; g0=$(get_gov); f0=$(md5of "$binpath")
  soc0=$(read_temp $SOC_TEMP); gpu0=$(read_temp $GPU_TEMP)

  ( cd "$PORT_DIR" && timeout -s KILL "$TIMEOUT" "$binpath" \
      $RES_ARGS --replay "user://$REPLAY" \
      --video-driver GLES3 --main-pack "$PCK" ) \
      > "$rdir/stdout.log" 2>&1
  local rc=$?
  local t1 soc1 gpu1 g1; t1=$(date +%s); g1=$(get_gov)
  soc1=$(read_temp $SOC_TEMP); gpu1=$(read_temp $GPU_TEMP)

  if [ -f "$perf" ]; then
    # copy immediately: next run overwrites it
    cp -f "$perf" "$rdir/replay_perf.json" && have=1
    if [ "$(stat -c %Y "$perf" 2>/dev/null || echo 0)" -lt "$seek" ]; then
      have=0
    fi
  fi

  printf '{"label":"%s","bin":"%s","md5":"%s","run":%d,"rc":%d,"seconds":%d,"roundtrip":%d,"temp_soc_before":%d,"temp_soc_after":%d,"temp_gpu_before":%d,"temp_gpu_after":%d,"gov_before":"%s","gov_after":"%s","perf_file":%d,"timeout":%d}\n' \
    "$label" "$binpath" "$f0" "$idx" "$rc" "$((t1-t0))" "$(( $(date +%s) - seek ))" \
    "$soc0" "$soc1" "$gpu0" "$gpu1" "$g0" "$g1" "$have" "$TIMEOUT" > "$rdir/meta.json"

  if grep -qaE 'Killed|Out of memory|oom-kill' "$rdir/stdout.log" 2>/dev/null; then
    echo "oom" > "$rdir/flag.oom"
  fi
  if [ "$rc" -eq 137 ]; then echo "killed137" > "$rdir/flag.killed"; fi
  [ "$have" -eq 1 ] || echo "noperf" > "$rdir/flag.noperf"

  echo "[$label run$idx] rc=$rc $((t1-t0))s soc ${soc0}->${soc1}C perf=$have"
}

cooldown() {
  local target="$1" waited=0 t
  while [ "$waited" -lt "$COOLDOWN_MAX" ]; do
    t=$(read_temp $SOC_TEMP)
    [ "$t" -le "$target" ] && return 0
    sleep 3; waited=$((waited+3))
  done
  echo "[cooldown] gave up after ${waited}s at ${t}C"
}

# ---------- snapshot engines + env ----------
GOV_RESTORE="for c in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo $GOV_ORIG > \$c; done"
cp -f "$ENGINES_FILE" "$SESSION_DIR/engines.txt"
{
  echo "{"
  echo "  \"session\": \"$SESSION\","
  echo "  \"kernel\": \"$(uname -r)\","
  echo "  \"arch\": \"$(uname -m)\","
  echo "  \"os_version\": \"$(grep -m1 OS_VERSION /etc/os-release | cut -d= -f2 | tr -d '"')\","
  echo "  \"governor_original\": \"$GOV_ORIG\","
  echo "  \"governor_pinned\": \"performance\","
  echo "  \"governor_restore_cmd\": \"$GOV_RESTORE\","
  echo "  \"replay\": \"$REPLAY\","
  echo "  \"replay_md5\": \"$(md5of "$USERDIR/$REPLAY")\","
  echo "  \"pck\": \"$PCK\","
  echo "  \"pck_md5\": \"$(md5of "$PCK")\","
  echo "  \"override_md5\": \"$(md5of "$PORT_DIR/override.cfg")\","
  echo "  \"shipped_engine_md5\": \"$(md5of "$PORT_DIR/odisea.frt.aarch64")\","
  echo "  \"runs\": $RUNS,"
  echo "  \"timeout_s\": $TIMEOUT,"
  echo "  \"cooldown_target_c\": $COOLDOWN_C,"
  echo "  \"result_epoch\": $(date +%s)"
  echo "}"
} > "$SESSION_DIR/meta.json"

# ---------- main loop: round-robin across engines so thermal drift is shared ----------
LABELS=(); PATHS=()
while IFS='|' read -r label binpath; do
  [ -n "$label" ] || continue
  case "$label" in \#*) continue;; esac
  [ -z "$ONLY" ] || [ "$label" = "$ONLY" ] || continue
  [ -f "$binpath" ] || { echo "[skip] $label: missing $binpath"; continue; }
  LABELS+=("$label"); PATHS+=("$binpath")
done < "$ENGINES_FILE"
if [ "${#LABELS[@]}" -eq 0 ]; then echo "no engines matched" >&2; exit 3; fi

round=1
while [ "$round" -le "$RUNS" ]; do
  echo "--- round $round/$RUNS (start-offset $(( (round-1) % ${#LABELS[@]} ))) ---"
  n=${#LABELS[@]}
  j=0
  while [ "$j" -lt "$n" ]; do
    k=$(( (j + (round - 1)) % n ))   # rotate start each round to cancel position bias
    run_one "${LABELS[$k]}" "${PATHS[$k]}" "$round"
    j=$((j+1))
  done
  round=$((round+1))
  [ "$round" -le "$RUNS" ] && cooldown "$COOLDOWN_C"
done

# ---------- restore dev.sh if something changed it ----------
if [ -f "$DEVSH_BAK" ]; then
  cur="$(md5of "$DEVSH")"
  if [ "$cur" != "$DEVSH_MD5_BEFORE" ]; then
    echo "[restore] dev.sh changed ($DEVSH_MD5_BEFORE -> $cur); restoring backup"
    cp -f "$DEVSH_BAK" "$DEVSH"
  fi
fi

echo
echo "temp after all runs (soc/gpu C): $(read_temp $SOC_TEMP) / $(read_temp $GPU_TEMP)"
echo "=== analyzing ==="
python3 "$SCRIPT_DIR/odisea_bench_analyze.py" "$SESSION_DIR" || exit 4
echo
echo "table:     $SESSION_DIR/table.txt"
echo "table json:$SESSION_DIR/table.json"
echo "raw json:  $SESSION_DIR/results.json"
echo "raw copies:$RAW_DIR/<label>/runN/replay_perf.json"
echo "dev.sh backup: $DEVSH_BAK"
