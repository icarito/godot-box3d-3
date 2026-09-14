#!/usr/bin/env bash
# Builds a Godot 3.6 binary carrying the Box3D module, reproducibly.
#
# Usage: scripts/build.sh <target>...
#   editor            X11 editor binary, also what scripts/test.sh runs locally
#   headless          server build, no X11 needed: the one a CI runner wants
#   linux-templates   Linux x86_64 export templates (release and debug)
#   windows-templates Windows x86_64 export templates, cross-compiled with MinGW
#   linux-arm64-templates  Linux ARM64 templates, built on an ARM64 host
#   frt-arm64-templates    FRT/SDL2 ARM64 templates for PortMaster handhelds
#   thegates-renderer      TheGates 3D browser renderer (x11 + the_gates module)
#   thegates-renderer-macos  Same renderer for macOS, universal binary (needs Xcode)
#   thegates-renderer-windows  Same renderer for Windows, cross-compiled with MinGW
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
# FRT es un "platform" out-of-tree (efornara/frt) que se clona en platform/frt.
# Pineado por la misma razon que el engine: un binario publicable tiene que ser
# reproducible. Los hooks que el engine necesita para conocer la plataforma van
# en patches/frt_platform_hooks.patch (6 archivos, todos inertes sin platform=frt).
FRT_REF="${FRT_REF:-01e53178e8aabd515bf327b27f847e3bb8b15251}"
FRT_URL="${FRT_URL:-https://github.com/efornara/frt.git}"
# El runtime de TheGates no es un platform sino un custom module (the_gates)
# que viajo como PR thegatesbrowser/thegates#1 sobre godot upstream sin
# parchear. Sus pins viven en scripts/thegates_env.sh, que arma el directorio
# .thegates-env/ que este target compila junto al modulo Box3D.
here="$(cd "$(dirname "$0")/.." && pwd)"
THEGATES_ENV_DIR="${THEGATES_ENV_DIR:-$here/.thegates-env}"
GODOT_DIR="${GODOT_DIR:-$(dirname "$here")/godot}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

if [ $# -eq 0 ]; then
	sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
	exit 1
fi

echo "==> Godot   $GODOT_DIR @ $GODOT_REF"
echo "==> Module  $here"

if [ ! -d "$GODOT_DIR/.git" ]; then
	# Blobless: the full 3.6 history is large and only one commit is built.
	git clone --filter=blob:none "$GODOT_URL" "$GODOT_DIR"
fi
# Only reach the network when the pinned commit is not here yet: a rebuild after
# touching a patch has no reason to fail offline.
if ! git -C "$GODOT_DIR" cat-file -e "$GODOT_REF^{commit}" 2>/dev/null; then
	git -C "$GODOT_DIR" fetch --quiet origin "$GODOT_REF" 2>/dev/null || git -C "$GODOT_DIR" fetch --quiet origin
fi
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
	(cd "$GODOT_DIR" && scons -j"$JOBS" \
		custom_modules="${CUSTOM_MODULES:-$here}" progress=no \
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
		frt-arm64-templates)
			# FRT usa SDL2 en vez de X11, que es lo que hace falta en los handhelds
			# de PortMaster: ROCKNIX no tiene GL de escritorio (su libGL.so.1 es un
			# stub) y el template x11 no arranca ahi. Cross-compilado con el
			# buildroot SDK de Godot, no con el toolchain del host: su glibc vieja
			# es lo que hace que el binario corra en cualquier CFW.
			: "${GODOT_SDK_LINUX_ARM64:?falta el SDK arm64 -- correr dentro de la imagen de build}"
			: "${SDL2_ARM64:?falta SDL2 arm64 -- correr dentro de la imagen de build}"
			frt_dir="$GODOT_DIR/platform/frt"
			if [ ! -d "$frt_dir/.git" ]; then
				git clone --quiet "$FRT_URL" "$frt_dir"
			fi
			if ! git -C "$frt_dir" cat-file -e "$FRT_REF^{commit}" 2>/dev/null; then
				git -C "$frt_dir" fetch --quiet origin
			fi
			git -C "$frt_dir" checkout --quiet --detach "$FRT_REF"
			echo "==> FRT     $frt_dir @ $FRT_REF"
			# Igual que el engine: los parches de FRT se reaplican sobre un checkout limpio.
			git -C "$frt_dir" checkout --quiet -- .
			for patch in "$here"/patches/frt/*.patch; do
				echo "==> Patch   frt/$(basename "$patch")"
				git -C "$frt_dir" apply "$patch"
			done
			(
				export PATH="$GODOT_SDK_LINUX_ARM64/bin:$SDL2_ARM64/bin:$PATH"
				# LINKFLAGS=-s es lo que usa el release de upstream FRT: production=yes
				# no strippea, y los simbolos son 7 MB de los 42 en una tarjeta SD.
				build platform=frt arch=arm64 target=release tools=no LINKFLAGS=-s
				build platform=frt arch=arm64 target=release_debug tools=no LINKFLAGS=-s
			)
			;;
		thegates-renderer)
			# El renderer que el launcher de TheGates baja del backend para los
			# gates que declaran godot_version = "3.6": un template x11 con el
			# module the_gates (IPC ZeroMQ + textura compartida via
			# GL_EXT_memory_object_fd) ademas de Box3D, mas los thirdparty que
			# el modulo compila desde .thegates-env/godot/thirdparty. libzmq
			# necesita excepciones, que Godot desactiva por defecto.
			"$here/scripts/thegates_env.sh" >/dev/null
			gates_modules="$THEGATES_ENV_DIR/modules/the_gates"
			build platform=x11 target=release tools=no disable_exceptions=no \
				custom_modules="$here,$gates_modules"
			build platform=x11 target=release_debug tools=no disable_exceptions=no \
				custom_modules="$here,$gates_modules"
			;;
		thegates-renderer-macos)
			# El mismo renderer para macOS: el launcher baja un unico binario
			# universal (Renderer-godot_v3.6.universal), asi que se compilan las
			# dos arquitecturas y se unen con lipo, como pack_macos.
			"$here/scripts/thegates_env.sh" >/dev/null
			gates_modules="$THEGATES_ENV_DIR/modules/the_gates"
			build platform=osx arch=x86_64 target=release tools=no disable_exceptions=no \
				custom_modules="$here,$gates_modules"
			build platform=osx arch=arm64 target=release tools=no disable_exceptions=no \
				custom_modules="$here,$gates_modules"
			lipo -create "$GODOT_DIR/bin/godot.osx.opt.x86_64" "$GODOT_DIR/bin/godot.osx.opt.arm64" \
				-output "$GODOT_DIR/bin/godot.osx.opt.thegates.universal"
			;;
		thegates-renderer-windows)
			"$here/scripts/thegates_env.sh" >/dev/null
			gates_modules="$THEGATES_ENV_DIR/modules/the_gates"
			build platform=windows target=release tools=no disable_exceptions=no \
				custom_modules="$here,$gates_modules"
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
			build platform=iphone arch=x86_64 target=release tools=no ios_simulator=yes
			build platform=iphone arch=arm64 target=release tools=no ios_simulator=yes
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
