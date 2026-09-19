# Spec: mute de AudioServer y swap de driver (Godot 3.x PR #63458) en godot-box3d-3

Estado: **implementado** en `patches/zzz_feature_audio_mute.patch`.

Feature: poder apagar el audio en runtime. `AudioServer.set_enabled(false)`
mutea entrada/salida y **cambia el driver activo al Dummy** (deja de correr el
hilo de audio del driver real y el daemon detrás: PulseAudio/ALSA/SDL); al
volver a `true` re-inicializa el driver real. Además hay flags de mute que el
motor prende solo: pérdida de foco, app pausada por el SO y silencio
prolongado. Es transversal a todos los rasterizadores y plataformas: no toca
render ni el módulo `box3d/`.

Fuente verificada: PR [`godotengine/godot#63458`](https://github.com/godotengine/godot/pull/63458)
(`lawnjelly`, abierto 2022-07-25, mergeado 2025-06-10, commit
`b94cd9bdc28274bc05ca4bd8307dff3f98565264`, 7 archivos, +365/−84) y el checkout
`../godot` (branch 3.6, pin `3.6.4-rc`, patches del repo encima).

## TL;DR

- **Aplica limpio sobre 3.6.4-rc** (sólo offsets de línea), incluidos todos
  nuestros patches encima. No hubo que adaptar nada.
- **No toca archivos compartidos** con ningún otro patch nuestro
  (`servers/audio_server.*`, `scene/main/scene_tree.cpp`, `editor/editor_node.cpp`
  no los modifica nadie más), así que el orden de aplicación es indiferente.
- **Es la palanca directa de CPU de audio en reposo.** Con el driver real
  apagado no queda hilo de mezcla ni cliente de audio corriendo; es
  complementario a elegir ALSA vs PulseAudio (ver
  `docs/lowend-audio-alsa-evaluation.md`), y probablemente pesa más en el
  perfil low-end que el driver elegido.

## Qué agrega

| Pieza | Detalle |
|-------|---------|
| `AudioServer.set_enabled(bool)` / `is_enabled()` | API nueva, bindeada a script. Mapea al flag `MUTE_FLAG_DISABLED`. |
| `AudioDriverManager::MuteFlags` | `DISABLED` (usuario), `FOCUS_LOSS`, `PAUSED`, `SILENCE`. `_mute_state` son los flags crudos; `_mute_state_final = _mute_state & _mute_state_mask`. |
| `set_mute_sensitivity(flag, on)` | Prende/apaga qué flags causan mute. El editor lo setea desde `EditorSettings`; el juego desde `ProjectSettings`. |
| `_set_driver(id)` | Si hay algún flag final activo, fuerza el último driver (el Dummy). Al limpiarse, re-`init()`/`start()` del driver deseado. |
| `SceneTree` | `NOTIFICATION_WM_FOCUS_IN/OUT` y `NOTIFICATION_APP_PAUSED/RESUMED` setean/limpian `FOCUS_LOSS` y `PAUSED`. |
| `AudioServer::update()` | Detecta silencio: `thread_get_channel_mix_buffer()` marca `last_sound_played_ms`; si pasan >10 s sin audio se prende `SILENCE`, y si vuelve a haber sonido se limpia. |
| ProjectSettings | `audio/muting/mute_driver` (false), `mute_on_pause` (true), `mute_on_silence` (false), `mute_on_focus_loss` (false). |
| EditorSettings | `interface/audio/muting/mute_driver`, `mute_on_silence`, `mute_on_pause`, `mute_on_focus_loss` (el editor mutea por defecto y despierta al reproducir). |
| Fix de paso | `AudioDriverDummy::finish()` ahora hace `samples_in = nullptr` para que un `init()` posterior no libere de nuevo. |

## Semántica de los flags

- Con **todos** los flags limpios (`_mute_state_final == 0`) el driver real
  corre y se procesa audio.
- Con cualquier flag activo, `_set_driver` cae al **Dummy** y
  `_driver_process()`/`_mix_step()` saltean el mezclado pesado
  (`is_audio_processing_allowed()`).
- `SILENCE` es la excepción: con el Dummy igual se mezcla (liviano) para poder
  detectar que volvió a haber sonido y despertar. Por eso el chequeo es
  `(_mute_state_final & ~SILENCE) == 0`.

## Cómo usarlo

```gdscript
AudioServer.set_enabled(false)   # mute + driver Dummy, minimiza CPU
AudioServer.set_enabled(true)    # re-init del driver real
```

Para el perfil low-end conviene dejar en `project.godot`:

```ini
[audio]
muting/mute_on_pause=true
muting/mute_on_silence=true
```

`mute_on_silence` puede meter latencia al arrancar el audio después del
silencio (el driver se re-inicializa), así que se evalúa por perfil; ver
`docs/lowend-audio-alsa-evaluation.md`.

## Prerrequisitos verificados en 3.6.4-rc

- `Main::is_project_manager()` existe (`main/main.h:52`) y es lo que distingue
  editor/project manager de un juego corriendo para elegir los defaults.
- `AudioDriverDummy` tiene `samples_in` (`servers/audio/audio_driver_dummy.h:44`),
  así que el fix de doble free aplica tal cual.
- `GLOBAL_DEF`, `GLOBAL_DEF_RST`, `EDITOR_DEF_RST` y el patrón
  `AudioDriverManager::add_driver()` por plataforma ya existen.

## Tests

`test_project/tests/m31_audio_mute_api.{gd,tscn}` (en `scripts/test.sh`):
bindings presentes, `is_enabled()` por defecto, las cuatro ProjectSettings
registradas, mute / idempotencia / unmute / toggle ida y vuelta. Corre con el
driver real (PulseAudio/ALSA) y con el Dummy sin romper. Verificado además con
`--audio-driver ALSA` y `--audio-driver PulseAudio` (ambos arrancan, drivers=3:
Dummy + Pulse + ALSA).

## Deuda conocida

- `last_sound_played_ms` se escribe desde el hilo de audio y se lee desde el
  main thread sin atómicos; el propio autor avisa que puede haber carreras.
- El swap de driver re-inicializa el backend real al desmutear; en algunos
  backends eso puede costar unos ms de latencia en el primer sonido.
- El editor usa sus propios defaults (muteado hasta que suena algo); no aplica
  al juego.