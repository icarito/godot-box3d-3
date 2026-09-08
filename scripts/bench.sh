#!/usr/bin/env bash
# Runs the stress benchmark scene under each compiled physics backend and
# prints the average physics time per frame. Swaps project.godot's
# 3d/physics_engine setting between runs and restores it afterwards.
# Usage: GODOT=/path/to/godot.x11.tools.64 scripts/bench.sh
set -uo pipefail

GODOT="${GODOT:-../godot/bin/godot.x11.tools.64}"
here="$(cd "$(dirname "$0")/.." && pwd)"
project="$here/test_project"
scene="res://bench/bench_stress.tscn"
key='3d/physics_engine'

backup="$(mktemp)"
cp "$project/project.godot" "$backup"
restore() { cp "$backup" "$project/project.godot"; }
trap restore EXIT

for engine in Box3D Bullet; do
	if grep -q "^$key=" "$project/project.godot"; then
		sed -i "s|^$key=.*|$key=\"$engine\"|" "$project/project.godot"
	else
		sed -i "s|^\[physics\]$|[physics]\n$key=\"$engine\"|" "$project/project.godot"
	fi
	echo "== $engine"
	"$GODOT" --path "$project" --no-window "$scene" 2>&1 | grep "^BENCH" || status=1
done
