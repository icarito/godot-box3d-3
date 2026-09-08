#!/usr/bin/env bash
# Runs the acceptance scenes headlessly against a Godot binary built with this module.
# Usage: GODOT=/path/to/godot.x11.tools.64 scripts/test.sh
set -uo pipefail

GODOT="${GODOT:-../godot/bin/godot.x11.tools.64}"
here="$(cd "$(dirname "$0")/.." && pwd)"

scenes=(
	res://tests/m2_falling_box.tscn
	res://tests/m3_kinematic.tscn
	res://tests/m4_shapes.tscn
	res://tests/m5_queries.tscn
	res://tests/m6_contacts.tscn
	res://tests/m6_areas.tscn
	res://tests/m7_joints.tscn
)

status=0
for scene in "${scenes[@]}"; do
	echo "== $scene"
	"$GODOT" --path "$here/test_project" --no-window "$scene" || status=1
done
exit $status
