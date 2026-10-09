#!/usr/bin/env bash
# Perfiles de build de godot-box3d-3: un conjunto de modulos por proyecto
# consumidor. Lo lee scripts/build.sh (source) y se elige con PROFILE=<nombre>
# (default: odisea).
#
# Un perfil decide SOLO que modulos entran al motor. La plataforma y tools=yes/no
# las elige el target (editor/headless/*-templates), y production/strip/LTO no
# cambian. La dieta es conservadora a proposito: se apagan modulos enteros que el
# proyecto no usa y se conserva production=yes + strip; no se toca disable_3d,
# disable_advanced_gui ni optimize=size sin medirlo en dispositivo.
#
#   odisea  motor completo (box3d + decal + imgui + slug); xmpp apagado. El juego.
#   xmpp    xat: modulo xmpp encendido, todo lo que xat no usa apagado.
#   lite    gdtk: FRT + imgui (implot3d) + slug; sin box3d/decal/xmpp, dieta 2D.
#
# Convencion de tags/releases, un release por perfil:
#   vX.Y.Z        -> odisea (matriz completa de Odisea + TheGates)
#   vX.Y.Z-xmpp   -> xmpp (lo que xat consume)
#   vX.Y.Z-lite   -> lite (binario FRT de gdtk)

# xat no usa: chat/UI 2D, su propio stack XMPP+SQLite y stb_vorbis/minimp3 para
# audio adjunto (ver xat/tools/build.sh). Todo lo demas es peso muerto.
XAT_DISABLED_MODULES="bullet csg gridmap enet upnp webrtc websocket webxr
	mobile_vr gdnative visual_script theora webm vorbis opus ogg gltf jsonrpc
	camera opensimplex raycast box3d decal imgui"

# gdtk: shell 2D sin fisica ni audio real (Dummy), sin red salvo StreamPeerTCP;
# conserva imgui (su UI) y slug (motor). Ver gdtk/deploy.sh.
GDTK_DISABLED_MODULES="bullet csg gridmap enet upnp webrtc websocket webxr
	mobile_vr gdnative visual_script theora webm vorbis opus ogg stb_vorbis
	minimp3 gltf jsonrpc camera opensimplex raycast box3d decal"

# Deja en el entorno, para build.sh:
#   PROFILE_MODULE_XMPP    yes/no  -> modulo xmpp (y su parche emoji en iOS)
#   PROFILE_MODULE_FLAGS   array   -> module_<m>_enabled=no por modulo podado
#   PROFILE_SCONS_FLAGS    array   -> flags propios del perfil
#   PROFILE_MODULES_EXTRA  string  -> overlay extra de custom_modules (gdtk)
# shellcheck disable=SC2034  # las variables las consume build.sh
profile_apply() { # profile_apply <odisea|xmpp|lite>
	local name="${1:-odisea}"
	PROFILE_MODULE_FLAGS=()
	PROFILE_SCONS_FLAGS=()
	PROFILE_MODULES_EXTRA=""
	PROFILE_DISABLED_MODULES=""
	case "$name" in
		odisea)
			PROFILE_MODULE_XMPP=no
			;;
		xmpp)
			PROFILE_MODULE_XMPP=yes
			PROFILE_DISABLED_MODULES="$XAT_DISABLED_MODULES"
			;;
		lite)
			PROFILE_MODULE_XMPP=no
			PROFILE_DISABLED_MODULES="$GDTK_DISABLED_MODULES"
			# Los modulos de gdtk (wayland, inotify) los trae scripts/gdtk_env.sh.
			PROFILE_MODULES_EXTRA="${GDTK_ENV_DIR:-$here/.gdtk-env}/modules"
			# imgui con ImPlot3D (gdtk pinta paneles): el default del fork es sin el.
			PROFILE_SCONS_FLAGS+=(imgui_implot3d=yes)
			# Binario portable: libstdc++ dinamica (ver gdtk/deploy.sh).
			PROFILE_SCONS_FLAGS+=(use_static_cpp=no)
			;;
		*)
			echo "profile_apply: perfil desconocido: $name" >&2
			return 2
			;;
	esac
	local m
	for m in $PROFILE_DISABLED_MODULES; do
		PROFILE_MODULE_FLAGS+=("module_${m}_enabled=no")
	done
	return 0
}
