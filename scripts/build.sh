#!/usr/bin/env bash
# Builds a Godot 3.6 binary carrying the Box3D module, reproducibly.
#
# Usage: scripts/build.sh <target>...
#   editor            X11 editor binary, also what scripts/test.sh runs locally
#   headless          server build, no X11 needed: the one a CI runner wants
#   linux-templates   Linux x86_64 export templates (release and debug)
#   windows-templates Windows x86_64 export templates, cross-compiled with MinGW
#   linux-arm64-templates  Linux ARM64 templates, built on an ARM64 host
#   html5-templates   WebAssembly templates, threaded and not (needs emsdk)
#   android-templates Android templates, all four ABIs (needs SDK + NDK)
#   macos-templates   macOS universal template (needs Xcode)
#   ios-templates     iOS template (needs Xcode)
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
	sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
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

# Godot ships these two as archives assembled from a template bundle plus the
# binaries, rather than as a single file scons emits.
pack_macos() {
	local bin="$GODOT_DIR/bin"
	rm -rf "$bin/osx_template.app"
	cp -r "$GODOT_DIR/misc/dist/osx_template.app" "$bin/"
	mkdir -p "$bin/osx_template.app/Contents/MacOS"
	lipo -create "$bin/godot.osx.opt.x86_64" "$bin/godot.osx.opt.arm64" \
		-output "$bin/osx_template.app/Contents/MacOS/godot_osx_release.64"
	lipo -create "$bin/godot.osx.opt.debug.x86_64" "$bin/godot.osx.opt.debug.arm64" \
		-output "$bin/osx_template.app/Contents/MacOS/godot_osx_debug.64"
	chmod +x "$bin/osx_template.app/Contents/MacOS"/godot_osx*
	(cd "$bin" && rm -f osx.zip && zip -q -r9 osx.zip osx_template.app)
}

pack_ios() {
	local bin="$GODOT_DIR/bin"
	rm -rf "$bin/ios_xcode"
	cp -r "$GODOT_DIR/misc/dist/ios_xcode" "$bin/"
	cp "$bin/libgodot.iphone.opt.arm64.a" \
		"$bin/ios_xcode/libgodot.iphone.release.xcframework/ios-arm64/libgodot.a"
	cp "$bin/libgodot.iphone.opt.debug.arm64.a" \
		"$bin/ios_xcode/libgodot.iphone.debug.xcframework/ios-arm64/libgodot.a"
	lipo -create "$bin/libgodot.iphone.opt.x86_64.simulator.a" \
		"$bin/libgodot.iphone.opt.arm64.simulator.a" \
		-output "$bin/ios_xcode/libgodot.iphone.release.xcframework/ios-arm64_x86_64-simulator/libgodot.a"
	cp "$bin/ios_xcode/libgodot.iphone.release.xcframework/ios-arm64_x86_64-simulator/libgodot.a" \
		"$bin/ios_xcode/libgodot.iphone.debug.xcframework/ios-arm64_x86_64-simulator/libgodot.a"
	(cd "$bin/ios_xcode" && rm -f ../iphone.zip && zip -q -r9 ../iphone.zip -- *)
}

# production=yes is what makes a binary publishable: no debug symbols (they are
# 90% of the file -- 520 MB against 42 MB for a Linux template) and a statically
# linked libstdc++, so the binary does not depend on the runner's toolchain
# version. LTO is off because it roughly doubles build time for a preliminary
# release. Set PRODUCTION=no when building to debug the module itself.
PRODUCTION="${PRODUCTION:-yes}"

build() { # build <scons args...>
	echo "==> scons $*"
	(cd "$GODOT_DIR" && scons -j"$JOBS" custom_modules="$here" progress=no \
		production="$PRODUCTION" lto=none "$@")
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
		linux-arm64-templates)
			# Built natively on an ARM64 runner; scons names the output by bit
			# width, so the artifact step is what tells the arm64 slot apart.
			build platform=x11 target=release tools=no
			build platform=x11 target=release_debug tools=no
			;;
		windows-templates)
			build platform=windows target=release tools=no
			build platform=windows target=release_debug tools=no
			;;
		html5-templates)
			# Odisea's preset is "HTML5 Threads", which reads the threads slot;
			# the plain one is built too so the .tpz has every slot filled.
			build platform=javascript target=release tools=no
			build platform=javascript target=release_debug tools=no
			build platform=javascript target=release tools=no threads_enabled=yes
			build platform=javascript target=release_debug tools=no threads_enabled=yes
			;;
		android-templates)
			for arch in armv7 arm64v8 x86 x86_64; do
				build platform=android target=release tools=no android_arch=$arch
				build platform=android target=release_debug tools=no android_arch=$arch
			done
			# Gradle wraps the .so files into the APKs Godot ships as templates.
			(cd "$GODOT_DIR/platform/android/java" && ./gradlew generateGodotTemplates)
			;;
		macos-templates)
			build platform=osx arch=x86_64 target=release tools=no
			build platform=osx arch=arm64 target=release tools=no
			build platform=osx arch=x86_64 target=release_debug tools=no
			build platform=osx arch=arm64 target=release_debug tools=no
			pack_macos
			;;
		ios-templates)
			build platform=iphone arch=arm64 target=release tools=no
			build platform=iphone arch=arm64 target=release_debug tools=no
			build platform=iphone arch=x86_64 target=release tools=no simulator=yes
			build platform=iphone arch=arm64 target=release tools=no simulator=yes
			pack_ios
			;;
		*)
			echo "!!! unknown target: $target" >&2
			exit 1
			;;
	esac
done

echo "==> Built:"
ls -1 "$GODOT_DIR/bin"
