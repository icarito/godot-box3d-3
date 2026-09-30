#!/usr/bin/env bash
# Descarga las fuentes del demo vectorial de Slug: una TrueType (cuadrática) y
# una CFF/OTF (cúbica, ejercita SlugCurveDecomposer). Idempotente.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FONT_DIR="$SCRIPT_DIR/fonts"
mkdir -p "$FONT_DIR"

fetch() {
	local url="$1"
	local dest="$2"
	if [[ -f "$dest" ]]; then
		echo "skip $(basename "$dest") (ya existe)"
		return 0
	fi
	echo "download $(basename "$dest")"
	curl -fL --retry 3 -o "$dest" "$url"
}

fetch \
	"https://github.com/notofonts/notofonts.github.io/raw/main/fonts/NotoSans/hinted/ttf/NotoSans-Regular.ttf" \
	"$FONT_DIR/NotoSans-Regular.ttf"
fetch \
	"https://github.com/adobe-fonts/source-sans/raw/release/OTF/SourceSans3-Regular.otf" \
	"$FONT_DIR/SourceSans3-Regular.otf"
fetch \
	"https://raw.githubusercontent.com/notofonts/latin-greek-cyrillic/main/OFL.txt" \
	"$FONT_DIR/OFL.txt"
fetch \
	"https://github.com/adobe-fonts/source-sans/raw/release/LICENSE.md" \
	"$FONT_DIR/SourceSans3-LICENSE.md"

echo "listo: fuentes en $FONT_DIR"
