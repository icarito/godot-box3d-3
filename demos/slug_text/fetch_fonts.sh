#!/usr/bin/env bash
# Descarga las fuentes Noto usadas por el demo, si no están ya en fonts/.
set -euo pipefail

dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/fonts"
mkdir -p "$dir"

fetch() {
	local name="$1" url="$2"
	if [[ -f "$dir/$name" ]]; then
		echo "ya existe: $name"
		return
	fi
	echo "descargando: $name"
	curl -fL -o "$dir/$name" "$url"
}

fetch NotoSans-Regular.ttf \
	"https://github.com/notofonts/notofonts.github.io/raw/main/fonts/NotoSans/hinted/ttf/NotoSans-Regular.ttf"
fetch NotoSerif-Regular.ttf \
	"https://github.com/notofonts/notofonts.github.io/raw/main/fonts/NotoSerif/hinted/ttf/NotoSerif-Regular.ttf"
fetch OFL.txt \
	"https://raw.githubusercontent.com/notofonts/latin-greek-cyrillic/main/OFL.txt"
