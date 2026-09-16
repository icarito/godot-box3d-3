#!/usr/bin/env bash
# Compara lado a lado el mismo frame de una escena renderizada por los dos
# binarios FRT: el camino ES (godot.frt.opt.tools.es) y el desktop-GL
# (godot.frt.opt.tools.x86_64). Genera <out>/<sufijo>_es.png, _gl.png y
# <out>/<sufijo>_sidebyside.png (izquierda ES, derecha desktop-GL).
#
# uso: scripts/compare_glitch.sh [escena-res] [frames] [out-dir]
#   GODOT_ES / GODOT_GL sobreescriben los binarios.
#   CAPTURE_PROJECT sobreescribe el proyecto (default test_project).
#   PARITY_OVERRIDE=0 deja de fijar la config del device en el proyecto de
#   captura (ver test_project/override_mobile_parity.cfg). Con la Fase 2
#   revertida el build desktop-GL ya reporta `mobile` como el arm64 y la
#   comparación da piso de ruido sin nada; esto queda como cinturón, porque si
#   el reporte de features vuelve a divergir el diff mide configuración
#   (depth/hdr y los otros overrides .mobile), no el driver.

here=$(cd "$(dirname "$0")/.." && pwd)
ES="${GODOT_ES:-$here/../godot/bin/godot.frt.opt.tools.es}"
GL="${GODOT_GL:-$here/../godot/bin/godot.frt.opt.tools.x86_64}"
PROJ="${CAPTURE_PROJECT:-$here/test_project}"
SCENE="${1:-res://demo/demo.tscn}"
FRAMES="${2:-90}"
OUT="${3:-/tmp/kilo/compare}"
SLUG=$(echo "$SCENE" | sed 's|res://||; s|[/_.]|_|g')
mkdir -p "$OUT"

PARITY_SRC="$here/test_project/override_mobile_parity.cfg"
OVR_DST="$PROJ/override.cfg"
OVR_SAVED=""

cleanup_override() {
	rm -f "$OVR_DST"
	if [ -n "$OVR_SAVED" ]; then
		mv -f "$OVR_SAVED" "$OVR_DST"
	fi
}

if [ "${PARITY_OVERRIDE:-1}" != "0" ] && [ -f "$PARITY_SRC" ]; then
	if [ -e "$OVR_DST" ]; then
		OVR_SAVED="$OVR_DST.parity-save.$$"
		mv -f "$OVR_DST" "$OVR_SAVED"
	fi
	cp "$PARITY_SRC" "$OVR_DST"
	echo "parity: override.cfg instalado en $PROJ (PARITY_OVERRIDE=0 lo desactiva)"
	trap cleanup_override EXIT INT TERM
fi

run_one() {
	local bin="$1" out="$2" name="$3"
	(
		cd "$PROJ" && \
		CAPTURE_SCENE="$SCENE" CAPTURE_OUT="$out" CAPTURE_FRAMES="$FRAMES" CAPTURE_PAUSE="${CAPTURE_PAUSE:-1}" \
		SDL_VIDEODRIVER=wayland "$bin" --path . --no-window -s "$here/test_project/capture_scene.gd"
	) > "$out.log" 2>&1
	if [ ! -f "$out" ]; then
		echo "FALLO $name:" >&2
		grep -iE "error|fail" "$out.log" | head -3 >&2
		return 1
	fi
}

run_one "$ES" "$OUT/${SLUG}_es.png" || exit 1
run_one "$GL" "$OUT/${SLUG}_gl.png" || exit 1

python3 - "$OUT/${SLUG}_es.png" "$OUT/${SLUG}_gl.png" "$OUT/${SLUG}_sidebyside.png" <<'EOF'
import sys
from PIL import Image, ImageDraw
a = Image.open(sys.argv[1]).convert("RGB")
b = Image.open(sys.argv[2]).convert("RGB")
w = max(a.width, b.width)
canvas = Image.new("RGB", (a.width + b.width + 4, max(a.height, b.height)), (30, 30, 30))
canvas.paste(a, (0, 0))
canvas.paste(b, (a.width + 4, 0))
d = ImageDraw.Draw(canvas)
d.text((8, 8), "ES", fill=(255, 255, 0))
d.text((a.width + 12, 8), "DESKTOP-GL", fill=(0, 255, 255))
canvas.save(sys.argv[3])
print(sys.argv[3])
EOF
