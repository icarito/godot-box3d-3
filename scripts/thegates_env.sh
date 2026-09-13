#!/usr/bin/env bash
# Prepara las fuentes del runtime de TheGates que thegates-renderer necesita.
#
# TheGates (thegatesbrowser) es un browser 3D cuyo launcher corre cada "gate"
# en un proceso renderer separado. Su runtime de Godot 3 no parchea el engine:
# es un custom module (the_gates) que viajo como PR thegatesbrowser/thegates#1
# y vive en la rama feature/godot3-runtime-support-*. El modulo espera los
# thirdparty que compila (libzmq, cppzmq, flingfd) dos niveles arriba en un
# directorio godot/thirdparty, donde los vende el fork Godot 4.5 de TheGates.
# Ambos motores compilan las mismas fuentes para no divergir en el wire
# format, asi que aca se traen del fork en vez de vendorar una segunda copia.
#
# Idempotente: si el modulo y los thirdparty ya estan, no toca la red, asi que
# un rebuild despues de tocar un patch no tiene por que fallar offline.
#
# Directorio que deja armado (SCsub de the_gates lo consume):
#   <env>/modules/the_gates/               el modulo out-of-tree
#   <env>/godot/thirdparty/{libzmq,cppzmq,flingfd,vulkan/include}
#
# THEGATES_ENV_DIR donde vive todo (default: <fork>/.thegates-env, gitignored).
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
env_dir="${THEGATES_ENV_DIR:-$here/.thegates-env}"

# Pins: moverlos cambia el binario y con el el protocolo de frames que habla
# el launcher. THEGATES_REF es el head del PR que agrego el runtime Godot 3.6;
# THEGATES_GODOT_REF es la cabeza de tg-4.5, de donde salen los thirdparty.
THEGATES_REF="${THEGATES_REF:-93b9f55a7bbf519673799fb221876842531c8147}"
THEGATES_URL="${THEGATES_URL:-https://github.com/icarito/thegates.git}"
THEGATES_GODOT_REF="${THEGATES_GODOT_REF:-aa5805a19e99bd2670cb05b2906962fa3fdb47a6}"
THEGATES_GODOT_URL="${THEGATES_GODOT_URL:-https://github.com/thegatesbrowser/godot.git}"

module="$env_dir/modules/the_gates"
thirdparty="$env_dir/godot/thirdparty"

# Trae directorios sueltos de un commit remoto sin bajar el resto del arbol:
# clone sin checkout mas un checkout selectivo hace que el filtro blobless
# baje los blobs de esos caminos y de nada mas. Cada <dir> copia su basename
# a <dest-parent>/<basename>.
extract() { # extract <url> <ref> <dest-parent> <dir>...
	local url="$1" ref="$2" dest_parent="$3"
	shift 3
	local tmp d
	tmp="$(mktemp -d)"
	git clone --quiet --no-checkout --filter=blob:none --depth 1 "$url" "$tmp/repo"
	if ! git -C "$tmp/repo" cat-file -e "$ref^{commit}" 2>/dev/null; then
		git -C "$tmp/repo" fetch --quiet --depth 1 --filter=blob:none origin "$ref"
	fi
	git -C "$tmp/repo" checkout --quiet "$ref" -- "$@"
	mkdir -p "$dest_parent"
	for d in "$@"; do
		rm -rf "$dest_parent/$(basename "$d")"
		mv "$tmp/repo/$d" "$dest_parent/$(basename "$d")"
	done
	rm -rf "$tmp"
}

if [ ! -f "$module/SCsub" ]; then
	echo "==> the_gates module $THEGATES_REF" >&2
	extract "$THEGATES_URL" "$THEGATES_REF" "$env_dir/modules" godot3-modules/the_gates
fi

if [ ! -d "$thirdparty/libzmq" ] || [ ! -d "$thirdparty/cppzmq" ] \
	|| [ ! -d "$thirdparty/flingfd" ]; then
	echo "==> TheGates fork thirdparty $THEGATES_GODOT_REF" >&2
	extract "$THEGATES_GODOT_URL" "$THEGATES_GODOT_REF" "$thirdparty" \
		thirdparty/libzmq thirdparty/cppzmq thirdparty/flingfd
fi

# Los headers de Vulkan los usa el module para medir la asignacion compartida
# (Windows, y Linux cuando el fd no informa su tamano).
if [ ! -d "$thirdparty/vulkan/include" ]; then
	echo "==> TheGates fork Vulkan headers $THEGATES_GODOT_REF" >&2
	extract "$THEGATES_GODOT_URL" "$THEGATES_GODOT_REF" "$thirdparty/vulkan" \
		thirdparty/vulkan/include
fi

if [ ! -f "$module/SCsub" ] || [ ! -d "$thirdparty/libzmq" ] \
	|| [ ! -d "$thirdparty/cppzmq" ] || [ ! -d "$thirdparty/flingfd" ] \
	|| [ ! -d "$thirdparty/vulkan/include" ]; then
	echo "!!! entorno thegates incompleto: faltan piezas en $env_dir" >&2
	exit 1
fi

echo "THEGATES_MODULE_DIR=$module"
echo "THEGATES_THIRDPARTY_DIR=$thirdparty"
