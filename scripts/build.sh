#!/usr/bin/env bash
# Builds a Godot 3.6 binary carrying the Box3D module, reproducibly.
#
# Usage: scripts/build.sh <target>...
#   editor            X11 editor binary, also what scripts/test.sh runs locally
#   headless          server build, no X11 needed: the one a CI runner wants
#   linux-templates   Linux x86_64 export templates (release and debug)
#   windows-templates Windows x86_64 export templates, cross-compiled with MinGW
#
# The Godot checkout is pinned: a custom module is only as reproducible as the
# engine it is compiled into. Override with GODOT_REF / GODOT_DIR.
set -euo pipefail

# Godot 3.6 branch, "Bump version to 3.6.4-rc". Engine version strings end up in
# the template directory name, so moving this pin changes what a project must
# install; see the README's release notes.
GODOT_REF="${GODOT_REF:-6371881f6742425cc14eaa367f18dd95955bf5e5}"
GODOT_URL="${GODOT_URL:-https://github.com/godotengine/godot.git}"
here="$(cd "$(dirname "$0")/.." && pwd)"
GODOT_DIR="${GODOT_DIR:-$(dirname "$here")/godot}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

if [ $# -eq 0 ]; then
	sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'
	exit 1
fi

echo "==> Godot   $GODOT_DIR @ $GODOT_REF"
echo "==> Module  $here"

if [ ! -d "$GODOT_DIR/.git" ]; then
	# Blobless: the full 3.6 history is large and only one commit is built.
	git clone --filter=blob:none "$GODOT_URL" "$GODOT_DIR"
fi
git -C "$GODOT_DIR" fetch --quiet origin "$GODOT_REF" 2>/dev/null || git -C "$GODOT_DIR" fetch --quiet origin
git -C "$GODOT_DIR" checkout --quiet --detach "$GODOT_REF"

# Patches are reapplied from a clean checkout each time, so a build never
# stacks them or silently skips one that stopped applying.
git -C "$GODOT_DIR" checkout --quiet -- .
for patch in "$here"/patches/*.patch; do
	echo "==> Patch   $(basename "$patch")"
	git -C "$GODOT_DIR" apply "$patch"
done

if [ ! -e "$here/box3d/thirdparty/box3d/include/box3d/box3d.h" ]; then
	echo "!!! box3d submodule missing: git submodule update --init --recursive" >&2
	exit 1
fi

build() { # build <scons args...>
	echo "==> scons $*"
	(cd "$GODOT_DIR" && scons -j"$JOBS" custom_modules="$here" progress=no "$@")
}

for target in "$@"; do
	case "$target" in
		editor)
			build platform=x11 target=release_debug tools=yes
			;;
		headless)
			# platform=server links no X11, so it runs on a bare CI runner. This
			# is the binary a project's test job should use.
			build platform=server target=release_debug tools=yes
			;;
		linux-templates)
			build platform=x11 target=release tools=no
			build platform=x11 target=release_debug tools=no
			;;
		windows-templates)
			build platform=windows target=release tools=no
			build platform=windows target=release_debug tools=no
			;;
		*)
			echo "!!! unknown target: $target" >&2
			exit 1
			;;
	esac
done

echo "==> Built:"
ls -1 "$GODOT_DIR/bin"
