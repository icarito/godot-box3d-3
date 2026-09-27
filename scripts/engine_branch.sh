#!/usr/bin/env bash
# Rama del motor: la fuente de verdad de los parches a Godot es una rama de git
# (ENGINE_BRANCH en ENGINE_URL, un commit por parche sobre GODOT_REF); patches/*.patch
# se GENERA desde ella y es lo que build.sh y el CI siguen aplicando.
#
#   scripts/engine_branch.sh import   crea/reescribe la rama desde patches/*.patch (bootstrap)
#   scripts/engine_branch.sh export   regenera patches/*.patch desde la rama
#   scripts/engine_branch.sh check    verifica que patches/ aplicado == árbol de la rama
#   scripts/engine_branch.sh push     publica la rama en ENGINE_URL
#
# Flujo diario: editar en el worktree de la rama (ENGINE_DIR), commitear (un commit por
# parche; fixup/rebase -i para tocar uno existente), `export`, `check`, commitear patches/.
# Cada commit lleva el trailer `Patch-File: <nombre>.patch`; un commit nuevo sin trailer
# toma el nombre de su asunto con más `z` que el último, para que el orden alfabético
# que usa build.sh sea el orden de la rama. Sólo los parches al motor: patches/frt/ y
# patches/box3d/ siguen a mano.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
eval "$(grep -E '^GODOT_(REF|URL)=' "$here/scripts/build.sh")"
GODOT_DIR="${GODOT_DIR:-$(dirname "$here")/godot}"      # clon de Godot (compartido)
ENGINE_DIR="${ENGINE_DIR:-$(dirname "$here")/godot-engine}" # worktree con la rama
ENGINE_BRANCH="${ENGINE_BRANCH:-box3d-3.6}"
ENGINE_URL="${ENGINE_URL:-git@github.com:icarito/godot.git}"
PATCHES="$here/patches"

g() { git -C "$ENGINE_DIR" "$@"; }

ensure_worktree() {
	if [ ! -e "$ENGINE_DIR/.git" ]; then
		git -C "$GODOT_DIR" cat-file -e "$GODOT_REF^{commit}" 2>/dev/null || git -C "$GODOT_DIR" fetch --quiet origin
		if git -C "$GODOT_DIR" show-ref --quiet "refs/heads/$ENGINE_BRANCH"; then
			git -C "$GODOT_DIR" worktree add --quiet "$ENGINE_DIR" "$ENGINE_BRANCH"
		else
			git -C "$GODOT_DIR" worktree add --quiet -b "$ENGINE_BRANCH" "$ENGINE_DIR" "$GODOT_REF"
		fi
	fi
}

# Mismo criterio que apply_patch de build.sh: borrar lo que el parche crea, luego git apply.
apply_to() { # apply_to <patch> <dir> [--index]
	local created
	created=$(awk '/^diff --git/ { f="" } /^new file mode/ { n=1 } /^\+\+\+ b\// && n { sub(/^\+\+\+ b\//, ""); print; n=0 }' "$1")
	while IFS= read -r f; do [ -n "$f" ] && rm -f "$2/$f"; done <<< "$created"
	git -C "$2" apply ${3:-} "$1"
}

cmd_import() {
	ensure_worktree
	[ -z "$(g status --porcelain)" ] || { echo "engine_branch: $ENGINE_DIR tiene cambios sin commitear" >&2; exit 1; }
	g checkout --quiet -B "$ENGINE_BRANCH" "$GODOT_REF"
	for p in "$PATCHES"/*.patch; do
		name="$(basename "$p")"
		apply_to "$p" "$ENGINE_DIR" --index
		g commit --quiet -m "${name%.patch}" -m "Patch-File: $name"
		echo "==> $name"
	done
	echo "engine_branch: $ENGINE_BRANCH = $(g rev-parse --short HEAD) ($(g rev-list --count "$GODOT_REF"..HEAD) commits sobre $GODOT_REF)"
}

cmd_export() {
	ensure_worktree
	local commits last="" out
	mapfile -t commits < <(g rev-list --reverse "$GODOT_REF".."$ENGINE_BRANCH")
	out="$(mktemp -d)"
	for c in "${commits[@]}"; do
		name="$(g log -1 --format='%(trailers:key=Patch-File,valueonly)' "$c" | head -1 | tr -d '[:space:]')"
		if [ -z "$name" ]; then
			# Commit nuevo: nombre desde el asunto, con más z que el último para quedar después.
			local z="${last%%[!z]*}" subj
			subj="$(g log -1 --format=%s "$c" | tr 'A-Z ' 'a-z_' | tr -cd 'a-z0-9_-' | cut -c1-60)"
			name="${z}z_${subj}.patch"
			echo "engine_branch: $(g rev-parse --short "$c") sin Patch-File -> $name (agregá el trailer al commit)" >&2
		fi
		if [ -n "$last" ] && [[ ! "$name" > "$last" ]]; then
			echo "engine_branch: orden roto: $name no va después de $last (build.sh aplica en orden alfabético)" >&2
			exit 1
		fi
		g diff --binary "$c^" "$c" > "$out/$name"
		last="$name"
	done
	rm -f "$PATCHES"/*.patch
	mv "$out"/*.patch "$PATCHES"/ && rmdir "$out"
	echo "engine_branch: ${#commits[@]} parches exportados a patches/"
}

cmd_check() {
	ensure_worktree
	local tmp; tmp="$(mktemp -d)/check"
	git -C "$GODOT_DIR" worktree add --quiet --detach "$tmp" "$GODOT_REF"
	for p in "$PATCHES"/*.patch; do apply_to "$p" "$tmp" >/dev/null; done
	git -C "$tmp" add -A
	local want have
	want="$(git -C "$GODOT_DIR" rev-parse "$ENGINE_BRANCH^{tree}")"; have="$(git -C "$tmp" write-tree)"
	git -C "$GODOT_DIR" worktree remove --force "$tmp"
	if [ "$want" = "$have" ]; then echo "engine_branch: OK, patches/ == $ENGINE_BRANCH"; else
		echo "engine_branch: DIFIEREN (patches/ -> $have, rama -> $want); correr export" >&2; exit 1; fi
}

cmd_push() {
	git -C "$GODOT_DIR" remote get-url box3d >/dev/null 2>&1 || git -C "$GODOT_DIR" remote add box3d "$ENGINE_URL"
	git -C "$GODOT_DIR" push box3d "$ENGINE_BRANCH"
}

case "${1:-}" in
	import) cmd_import ;; export) cmd_export ;; check) cmd_check ;; push) cmd_push ;;
	*) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
