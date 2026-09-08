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
	res://tests/m8_rays.tscn
	res://tests/m9_trimesh_walk.tscn
	res://tests/m10_game_repro.tscn
	res://tests/m11_unstick.tscn
	res://tests/m12_penetrated.tscn
	res://tests/m13_trigger_layers.tscn
	res://tests/m14_shared_trimesh.tscn
	res://tests/m15_shape_scale.tscn
	res://tests/m16_area_reshape.tscn
	res://tests/m17_cryopod.tscn
	res://tests/m18_scaled_hull.tscn
	res://tests/m20_body_scale.tscn
	res://tests/m21_character_mover.tscn
	res://tests/m22_trimesh_jitter.tscn
)

status=0
for scene in "${scenes[@]}"; do
	echo "== $scene"
	"$GODOT" --path "$here/test_project" --no-window "$scene" || status=1
done
exit $status
