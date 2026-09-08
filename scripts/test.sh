#!/usr/bin/env bash
# Runs the acceptance scenes headlessly against a Godot binary built with this module.
# Usage: GODOT=/path/to/godot.x11.tools.64 scripts/test.sh
set -euo pipefail

GODOT="${GODOT:-../godot/bin/godot.x11.tools.64}"
here="$(cd "$(dirname "$0")/.." && pwd)"

"$GODOT" --path "$here/test_project" --no-window
