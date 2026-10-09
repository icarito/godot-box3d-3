# Build profiles

Este fork lo consumen tres proyectos con necesidades distintas, así que no hay
un único build: cada perfil compila solo los módulos que su consumidor usa.

| Consumidor | Perfil | Tag | Qué consume |
|------------|--------|-----|-------------|
| Odisea (juego 3D) | `odisea` | `vX.Y.Z` | motor completo: editor, headless, `.tpz` de todas las plataformas, FRT, TheGates |
| xat (cliente XMPP) | `xmpp` | `vX.Y.Z-xmpp` | templates `*_xmpp`: headless, Android, macOS, iOS |
| gdtk (shell/compositor Wayland) | `lite` | `vX.Y.Z-lite` | `godot.box3d.frt.gdtk.*` (editor + runtime FRT x86_64) |

## Cómo funciona

`scripts/profiles.sh` define un conjunto de módulos por perfil. `scripts/build.sh`
lo lee (`PROFILE=<nombre>`, default `odisea`) y lo traduce a argumentos de scons:

- `module_<m>_enabled=no` por cada módulo que el perfil no usa.
- `module_xmpp_enabled=yes|no` siempre explícito: `modules/xmpp` está commiteado
  en el overlay y, con `modules_enabled_by_default=yes`, sin el flag se encendería
  en todos los builds.
- `PROFILE_SCONS_FLAGS` (p. ej. `imgui_implot3d=yes`, `use_static_cpp=no` en `lite`).
- `custom_modules`: `$here` para todos; `lite` agrega los módulos de gdtk.

El perfil decide **solo qué módulos entran**. La plataforma y `tools=yes/no` los
elige el target (`editor`, `headless`, `*-templates`, `gdtk-lite`).

```
scripts/build.sh editor                       # perfil odisea (default)
PROFILE=xmpp scripts/build.sh headless        # xat
PROFILE=lite scripts/build.sh gdtk-lite       # gdtk
MODULE_XMPP=yes scripts/build.sh ...          # alias legacy de PROFILE=xmpp
```

## Los perfiles

- **odisea** — motor completo (box3d + decal + imgui + slug), xmpp apagado. Es el
  comportamiento previo del fork.
- **xmpp** — xat: módulo xmpp encendido; apaga lo que un cliente de chat no usa
  (`bullet csg gridmap enet upnp webrtc websocket webxr mobile_vr gdnative
  visual_script theora webm vorbis opus ogg gltf jsonrpc camera opensimplex
  raycast box3d decal imgui`). Conserva `stb_vorbis` + `minimp3` (audio adjunto) y
  `mbedtls` (TLS del stack XMPP).
- **lite** — gdtk: FRT + imgui (`implot3d=yes`) + slug, sin box3d/decal/xmpp;
  apaga además `stb_vorbis minimp3` (audio Dummy). `scripts/gdtk_env.sh` trae su
  módulo `wayland` desde `github.com/icarito/gdtk` (pineado); `modules/inotify`
  se incluye solo si está commiteado en el ref (es opcional: el shell cae a
  fallback por polling, `shell/apps.gd`).

### La dieta es conservadora

Se apagan módulos enteros que el consumidor no usa y se mantiene
`production=yes` + strip. **No** se toca `disable_3d`, `disable_advanced_gui` ni
`optimize=size` sin medirlo en dispositivo (ver la nota de LTO para arm64 en
`scripts/build.sh`). Cuando haya mediciones, un perfil puede sumar esos flags en
`PROFILE_SCONS_FLAGS` sin tocar el resto.

## Releases y tags

Un release por perfil. Empujar un tag construye solo su matriz:

- `vX.Y.Z` → `release.yml` (guard: no corre si el tag termina en `-xmpp`/`-lite`).
  Matriz completa de Odisea + TheGates.
- `vX.Y.Z-xmpp` → `release-xmpp.yml` (headless, Android, macOS, iOS con `profile:
  xmpp`), publica los assets `*_xmpp` que xat baja.
- `vX.Y.Z-lite` → `release-lite.yml` (contenedor Arch por las deps de wlroots),
  publica `godot.box3d.frt.gdtk.*`.

Cada consumidor pinea su tag (p. ej. xat en `.github/box3d_release`:
`v0.5.5-xmpp`). El workflow reutilizable `build-target.yml` recibe `profile` y
renombra los assets según el perfil.

### CI de gdtk

El módulo `wayland` exige `wlroots-0.20`, `libeis-1.0` y `libsystemd`, que no están
en Ubuntu 22.04: `release-lite.yml` compila dentro de un contenedor
`archlinux:base-devel`. El paso de verificación comprueba que el binario traiga
`WaylandCompositor`; si falta wlroots-0.20, el `can_build()` del módulo devuelve
False y el build falla a propósito en vez de publicar un binario sin compositor.

## Agregar un perfil

1. Sumar la lista de módulos a apagar y una rama en `profile_apply`
   (`scripts/profiles.sh`).
2. Documentarlo en la tabla de arriba y en el README.
3. Agregarlo a la matriz/renombre de `build-target.yml` si publica templates
   (sufijo de asset) o crear un `<perfil>` target en `build.sh`.
4. Elegir el tag (`v*-<perfil>`) y, si no es un sufijo ya conocido, sumarlo al
   `guard` de `release.yml`.
