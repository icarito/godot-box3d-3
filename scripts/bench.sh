#!/usr/bin/env bash
# Runs the stress benchmark scene under each compiled physics backend and
# prints the average physics time per frame. Swaps project.godot's
# 3d/physics_engine setting between runs and restores it afterwards.
# Box3D is measured twice: with the default 4 sub-steps and with 1.
# Usage: GODOT=/path/to/godot.x11.tools.64 scripts/bench.sh
set -uo pipefail

GODOT="${GODOT:-../godot/bin/godot.x11.tools.64}"
here="$(cd "$(dirname "$0")/.." && pwd)"
project="$here/test_project"
scene="res://bench/bench_stress.tscn"
key='3d/physics_engine'
substeps_key='3d/box3d_substeps'

backup="$(mktemp)"
cp "$project/project.godot" "$backup"
restore() { cp "$backup" "$project/project.godot"; }
trap restore EXIT

set_engine() {
	if grep -q "^$key=" "$project/project.godot"; then
		sed -i "s|^$key=.*|$key=\"$1\"|" "$project/project.godot"
	else
		sed -i "s|^\[physics\]$|[physics]\n$key=\"$1\"|" "$project/project.godot"
	fi
}

set_substeps() {
	# Empty argument removes the line so the module default applies.
	if grep -q "^$substeps_key=" "$project/project.godot"; then
		sed -i "/^$substeps_key=/d" "$project/project.godot"
	fi
	if [ -n "$1" ]; then
		sed -i "s|^\[physics\]$|[physics]\n$substeps_key=$1|" "$project/project.godot"
	fi
}

for config in "Box3D 1" "Box3D 4" "Bullet"; do
	read -r engine substeps <<< "$config"
	set_engine "$engine"
	if [ -n "$substeps" ]; then
		set_substeps "$substeps"
		echo "== $engine (substeps=$substeps)"
	else
		set_substeps ""
		echo "== $engine"
	fi
	"$GODOT" --path "$project" --no-window "$scene" 2>&1 | grep "^BENCH" || true
done
