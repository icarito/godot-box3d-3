# Evaluación: backend de audio en el perfil low-end (ROCKNIX / angel.local)

Pregunta: en PortMaster/ROCKNIX, ¿SDL2 elige ALSA o Pulse?, ¿cuál es más
liviano? Respuesta medida en el device (no inferida): **elige Pulse —el
protocolo PulseAudio servido por `pipewire-pulse`— y ALSA directo es más liviano
en capas, pero en esta imagen `SDL_AUDIODRIVER=alsa` NO evita PipeWire**, porque
el `default` de ALSA *es* el plugin de PipeWire. Los ahorros reales están en el
resampleo y el buffer, no en el nombre del driver.

## Evidencia medida en `angel.local`

ROCKNIX (imagen 2026-09-01, kernel 7.1.2, codec rk817, pipewire 17):

```text
# procesos de audio
339 /usr/bin/pipewire
349 /usr/bin/wireplumber -p main-systemwide
366 /usr/bin/pipewire-pulse
# no hay binario pulseaudio: command -v pulseaudio -> NO_PULSEAUDIO_BIN
```

El juego corriendo:

```text
8417 /roms/ports/odisea/odisea.frt.aarch64 --resolution 640x480 -f \
      --video-driver GLES3 --main-pack odisea.pck
env: SDL_AUDIODRIVER=pulseaudio   SDL_VIDEODRIVER=wayland
threads: SDLAudioP1, PulseMainloop, PulseHotplug
fd: 11 -> /memfd:pulseaudio
```

O sea: SDL2 abre el backend **pulseaudio** (forzado por el launcher), conecta a
`pipewire-pulse` (socket `/var/run/0-runtime-dir/pulse/native`) y el hilo
`SDLAudioP1` está vivo. **No es ALSA directo.** La cadena completa es:

```text
Godot → AudioDriverSDL2 → libpulse → pipewire-pulse → grafo PipeWire → ALSA → rk817
```

Costo de esa capa (2 ventanas de 5 s, `HZ=100`, deltas de utime+stime):

| Proceso | % de un core |
|---------|--------------|
| `pipewire` (grafo) | ~14% |
| `pipewire-pulse` (protocolo) | ~25% |
| `wireplumber` | ~0% |
| `odisea.frt.aarch64` | ~129% |

`pipewire-pulse`+`pipewire` ≈ **0.4 core** en un SoC de gama baja; no es ruido.

## Por qué ALSA no evade PipeWire acá

```text
# aplay -L (recortado)
pipewire
    PipeWire Sound Server
default
    Default ALSA Output (currently PipeWire Media Server)
sysdefault:CARD=rk817int
    rk817_int, ff070000.i2s-rk817-hifi rk817-hifi-0
```

El `default` de ALSA es el plugin de PipeWire. SDL2 abre `default` (o `AUDIODEV`
si está seteado), así que `SDL_AUDIODRIVER=alsa` **sigue pasando por PipeWire**,
con el mismo grafo y el mismo resampleo: no baja el costo. Para saltarlo de
verdad habría que apuntar SDL a `sysdefault:CARD=rk817int` o `hw:0,0` con
`AUDIODEV`, y ahí:

- PipeWire normalmente ya tiene tomado el PCM del rk817 → el open puede dar
  `EBUSY`.
- Se pierde el volumen/mezcla del sistema (PipeWire deja de mezclar el juego con
  el resto), y un xrun del juego corta todo.

No recomendado en ROCKNIX.

## Qué es "más liviano"

De menor a mayor cantidad de capas:

1. **ALSA directo a `hw`/`sysdefault`** (`libasound`, sin daemon). Es el más
   liviano, pero no mezcla y pelea por el device.
2. **ALSA vía plugin de PipeWire** (`default` en esta imagen): un hop más, misma
   verdad de reloj que el grafo.
3. **Protocolo Pulse sobre `pipewire-pulse`** (lo que usa hoy SDL): el que más
   suma (cliente libpulse + servidor pulse + grafo + resampleo).

Con PipeWire como sistema de audio, la comparación práctica no es "ALSA vs
Pulse" sino **cuánto resampleo y cuántos wakeups** hace el grafo. Config actual:

```text
# pw-metadata -n settings
clock.rate = 48000      clock.allowed-rates = [ 48000 ]
clock.quantum = 1024    clock.min-quantum = 32   clock.max-quantum = 2048
```

El juego mezcla a **44100** (Godot default; Odisea no overridea `audio/mix_rate`),
así que PipeWire **resamplea 44100→48000** para cada callback. En un Cortex-A35
eso explica buena parte del CPU de `pipewire-pulse`.

## Recomendación (actualizada con lo medido)

1. **`audio/mix_rate=48000` en el perfil low-end** (y en general en el device):
   alinea el juego con `clock.rate` y elimina el resampleo por callback. Es el
   cambio más barato y con efecto más directo sobre `pipewire-pulse`.
2. **`audio/output_latency` 30-50 ms** en el perfil low-end: menos callbacks por
   segundo (menos trabajo en Godot, en pipewire-pulse y en el grafo).
3. **`audio/muting/mute_on_silence=true` + `mute_on_pause=true`** (feature del
   PR #63458): en reposo se corta el stream y bajan los tres procesos. Ver
   `docs/audio-mute-backport-spec.md`.
4. **No forzar `SDL_AUDIODRIVER=alsa`** para "ahorrar": en esta imagen sigue
   pasando por PipeWire. Si se quisiera medir el bypass real, hacerlo con
   `AUDIODEV=sysdefault:CARD=rk817int` en un branch de prueba y midiendo EBUSY y
   pérdida de mezcla.
5. Opcional: subir `clock.force-quantum` (1024→2048) baja wakeups pero sube
   latencia y afecta a todo el sistema; medir antes.

## Plan de medición para validar 1-3

```sh
# baseline y después de cada cambio: CPU de los 3 procesos de audio + juego
for p in $(pgrep -x pipewire) $(pgrep -x pipewire-pulse) $(pgrep -x odisea.frt.aarc); do
  awk -v p=$p '{print p, $14+$15}' /proc/$p/stat
done   # dos veces separadas 5 s; delta/(100*5) = % de core
# y escuchar xruns/glitches al subir output_latency
```

## Estructura del audio (recordatorio)

| Camino | Driver | Cómo se elige |
|--------|--------|---------------|
| x11 desktop/editor | PulseAudio o ALSA de Godot | `--audio-driver ALSA` (verificado, drivers=3) |
| FRT handheld (Odisea low-end) | `AudioDriverSDL2` → pulse/pipewire | `SDL_AUDIODRIVER` (el launcher fuerza `pulseaudio`) |
| Android/iOS/Windows | driver propio | n/a |

Godot en FRT no compila ni registra los drivers ALSA/Pulse propios
(`platform/frt/frt_godot.cc` sólo registra `AudioDriverSDL2`), así que el
backend lo elige SDL2 y en `angel.local` es Pulse sobre `pipewire-pulse`.