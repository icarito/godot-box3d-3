#!/usr/bin/env bash
# Prepara el toolchain de cross-compilation que frt-arm64-templates necesita:
# el SDK buildroot de Godot (aarch64) y SDL2 compilada contra ese SDK. Es la
# misma receta del Dockerfile de FRT (platform/frt/scripts/Dockerfile),
# replicada para correr en un runner de CI o en el host sin Docker.
#
# Idempotente: si el SDK o la SDL2 ya estan en el directorio de entorno, no
# vuelve a bajar ni compilar nada, asi que un cache de CI del directorio entero
# ahorra la descarga y la compilacion de SDL2 en cada corrida.
#
# Variables que deja listas (build.sh las consume):
#   GODOT_SDK_LINUX_ARM64  SDK buildroot aarch64 reubicado
#   SDL2_ARM64             SDL2 userland compilada para aarch64
#
# FRT_ENV_DIR donde vive todo (default: <fork>/.frt-env, gitignored).
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
env_dir="${FRT_ENV_DIR:-$here/.frt-env}"

# Pins iguales a los del Dockerfile de FRT; moverlos cambia el binario.
SDK_VER="godot-2023.08.x-4"
SDK_URL="https://github.com/godotengine/buildroot/releases/download/$SDK_VER/aarch64-godot-linux-gnu_sdk-buildroot.tar.bz2"
SDL2_VER="2.32.10"
SDL2_URL="https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VER/SDL2-$SDL2_VER.tar.gz"

sdk="$env_dir/aarch64-godot-linux-gnu_sdk-buildroot"
sdl2="$env_dir/sdl2/arm64"

if [ ! -x "$sdk/bin/gcc" ]; then
	echo "==> SDK buildroot aarch64 $SDK_VER" >&2
	mkdir -p "$env_dir"
	curl -fsSL "$SDK_URL" | tar -xj -C "$env_dir"
	(cd "$sdk" && ./relocate-sdk.sh >/dev/null)
fi

if [ ! -x "$sdl2/bin/sdl2-config" ]; then
	echo "==> SDL2 $SDL2_VER (aarch64, shared)" >&2
	tmp="$(mktemp -d)"
	trap 'rm -rf "$tmp"' EXIT
	curl -fsSL "$SDL2_URL" | tar -xz -C "$tmp"
	mkdir -p "$sdl2" "$tmp/obj"
	(
		cd "$tmp/obj"
		PATH="$sdk/bin:$PATH" "../SDL2-$SDL2_VER/configure" \
			--prefix="$sdl2" --host=aarch64-linux-gnu >/dev/null
		PATH="$sdk/bin:$PATH" make -j"$(nproc 2>/dev/null || echo 4)" >/dev/null
		PATH="$sdk/bin:$PATH" make install >/dev/null 2>&1 || true
	)
fi

echo "GODOT_SDK_LINUX_ARM64=$sdk"
echo "SDL2_ARM64=$sdl2"
