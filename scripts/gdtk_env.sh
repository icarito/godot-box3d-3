#!/usr/bin/env bash
# Prepara los modulos out-of-tree de gdtk que el perfil lite compila.
#
# gdtk (https://github.com/icarito/gdtk) es un shell/compositor Wayland sobre
# este fork: su compositor (wlroots embebido) y su watcher de ficheros son
# modulos nativos que viven en SU repo, no aqui. El perfil lite del fork los
# trae pineados para poder publicar un binario FRT que gdtk ejecuta, igual que
# scripts/thegates_env.sh hace con the_gates.
#
# Deja armado (custom_modules del perfil lite):
#   <env>/modules/wayland/    compositor wlroots + portal de input remoto (libei)
#   <env>/modules/inotify/    watcher de ficheros del shell
#
# GDTK_ENV_DIR (default <fork>/.gdtk-env, gitignored). El modulo wayland exige en
# el host: wlroots-0.20, libeis-1.0, wayland-server/client, xkbcommon, pixman,
# libsystemd y egl (pkg-config). En CI corren en un contenedor Arch; ver
# .github/workflows/release-lite.yml.
#
# Idempotente: si los modulos ya estan, no toca la red.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
env_dir="${GDTK_ENV_DIR:-$here/.gdtk-env}"
module_dir="$env_dir/modules"

# Pin: moverlo cambia el binario y el protocolo que habla con los clientes gdtk.
GDTK_REF="${GDTK_REF:-039980053fcfd63b466f88e0c36b26783860b24c}"
GDTK_URL="${GDTK_URL:-https://github.com/icarito/gdtk.git}"

if [ -f "$module_dir/wayland/SCsub" ] && [ -f "$module_dir/inotify/SCsub" ]; then
	exit 0
fi

echo "==> gdtk modules $GDTK_REF" >&2
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Trae solo esos dos directorios del commit pineado: clone sin checkout +
# checkout selectivo con filtro blobless baja los blobs de esos caminos y nada
# mas (mismo truco que scripts/thegates_env.sh).
git clone --quiet --no-checkout --filter=blob:none --depth 1 "$GDTK_URL" "$tmp/repo"
if ! git -C "$tmp/repo" cat-file -e "$GDTK_REF^{commit}" 2>/dev/null; then
	git -C "$tmp/repo" fetch --quiet --depth 1 --filter=blob:none origin "$GDTK_REF"
fi
git -C "$tmp/repo" checkout --quiet "$GDTK_REF" -- modules/wayland modules/inotify

rm -rf "$module_dir"
mkdir -p "$module_dir"
mv "$tmp/repo/modules/wayland" "$tmp/repo/modules/inotify" "$module_dir/"
# Los .o que gdtk dejo en su arbol de desarrollo no viajan.
find "$module_dir" -name '*.o' -delete

echo "==> gdtk modules ready in $module_dir" >&2
