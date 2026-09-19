# Evaluación: backend ALSA para audio de bajo overhead en el perfil low-end

Pregunta: ¿podemos usar el backend ALSA para bajar el costo de audio en el
perfil low-end de Odisea? Respuesta corta: **en el handheld ya estamos, de
hecho, sobre ALSA —vía SDL2, no vía el driver ALSA de Godot— y la palanca que
importa no es el driver sino el silencio/pausa y el tamaño de buffer.** Abajo
el porqué, con la evidencia en el árbol.

## Cómo se arma el audio hoy

### Build x11 (editor/desktop, `godot.box3d.linux.x86_64.editor`)

Godot 3.6 trae los dos drivers: `drivers/pulseaudio/audio_driver_pulseaudio.cpp`
y `drivers/alsa/audio_driver_alsa.cpp`. La plataforma x11 registra **PulseAudio
primero y ALSA como fallback**:

```cpp
// platform/x11/os_x11.cpp:4509
OS_X11::OS_X11() {
#ifdef PULSEAUDIO_ENABLED
	AudioDriverManager::add_driver(&driver_pulseaudio);
#endif
#ifdef ALSA_ENABLED
	AudioDriverManager::add_driver(&driver_alsa);
#endif
```

En nuestra máquina el binario x11 reporta `audio_drivers=3` (Dummy +
PulseAudio + ALSA) y verifiqué que arranca con cualquiera de los dos:
`--audio-driver ALSA` y `--audio-driver PulseAudio`, con `m31_audio_mute_api`
en PASS en ambos.

### Template handheld (`godot.box3d.frt.arm64.release`), el que corre Odisea en ROCKNIX

La plataforma FRT registra **un solo driver, `AudioDriverSDL2`**, y no compila
ni registra los de Godot:

```cpp
// platform/frt/frt_godot.cc:155
Godot3_OS() : os_(this) {
	AudioDriverManager::add_driver(&audio_driver_);   // AudioDriverSDL2
	...
}

// platform/frt/frt_godot.cc:42
Error init() override {
	mix_rate_ = GLOBAL_GET("audio/mix_rate");
	const int latency = GLOBAL_GET("audio/output_latency");
	const int samples = closest_power_of_2(latency * mix_rate_ / 1000);
	return audio_.init(mix_rate_, samples) ? OK : ERR_CANT_OPEN;
}
```

y `Audio::init()` (`platform/frt/sdl2_adapter.h:83`) abre con `SDL_OpenAudio`
(`AUDIO_S16`, 2 canales, `desired.samples = samples`). Es decir: **`--audio-driver
ALSA` no existe en el handheld**; el backend lo elige SDL2. SDL2 en Linux prueba
PulseAudio y cae a ALSA; en un CFW sin PulseAudio (lo típico en ROCKNIX) ya
termina en ALSA directo. `SDL_AUDIODRIVER=alsa` fuerza ese camino si hubiera
Pulse presente.

En resumen:

| Camino | Driver | Cómo se elige ALSA |
|--------|--------|--------------------|
| x11 desktop/editor | PulseAudio o ALSA (Godot) | `--audio-driver ALSA` (funciona, verificado) |
| FRT handheld (Odisea low-end) | SDL2 → Pulse/ALSA (no Godot) | `SDL_AUDIODRIVER=alsa` en el launcher |
| Android/iOS/Windows | driver propio | n/a |

## Qué cuesta cada camino

- **ALSA directo** habla con `snd_pcm` en el kernel: sin daemon ni protocolo ni
  resampling del lado del cliente. Menos overhead y latencia, a cambio de acceso
  más exclusivo y sin mezcla por app.
- **PulseAudio** suma un proceso/demonio, el protocolo cliente-servidor y a
  veces resampleo. Da mezcla y cambio de dispositivo en caliente.
- **SDL2** es una capa fina arriba de uno u otro; su overhead propio es chico.
- El costo del hilo de audio depende sobre todo de la **frecuencia de callbacks**,
  que la fija `audio/output_latency`. Con el default (15 ms, 44100 Hz):
  `closest_power_of_2(15*44100/1000) = 512` frames → ~86 callbacks/s; con 30 ms
  baja a ~43/s y con 50 ms a ~26/s. En RK3326/RK3566 eso puede notarse más que
  Pulse vs ALSA.
- Odisea hoy no overridea `mix_rate` ni `output_latency` (`src/project.godot`
  `[audio]` sólo trae `output_latency.web` y `driver/output_latency.Android`),
  así que usa 44100/15 ms.

## Recomendación para el perfil low-end

1. **No agregar `AudioDriverALSA` a FRT.** SDL2 ya llega a ALSA y mantener dos
   drivers duplica caminos sin ganancia clara. Si se quisiera el driver ALSA de
   Godot en FRT, es registrar `AudioDriverALSA` en `frt_godot.cc`, pero el
   adaptador SDL2 ya es la ruta de audio de esa plataforma.
2. **Probar `SDL_AUDIODRIVER=alsa` en el launcher del perfil low-end** (no en
   todos los perfiles). Si el CFW no tiene Pulse, no cambia nada; si lo tiene,
   evita el daemon. Medir antes de adoptarlo fijo.
3. **Subir `audio/output_latency` a 30-50 ms en el perfil low-end** mientras el
   gameplay lo tolere; es la palanca más determinista de CPU de audio. Un
   `override_mobile_parity.cfg`-style por perfil.
4. **Integrar el mute de #63458 y prender `audio/muting/mute_on_silence=true` y
   `mute_on_pause=true` en el perfil low-end.** Con el driver apagado en reposo
   desaparece el hilo real de audio (y el daemon) durante las pausas; en un juego
   con tramos largos sin música esto pesa más que elegir ALSA. Ver
   `docs/audio-mute-backport-spec.md`.
5. **Para el build x11/x86 de escritorio**, dejar ALSA como opción explícita
   (`--audio-driver ALSA`), no como default: en desktop Pulse da mezcla entre
   apps y cambio de dispositivo, que no queremos perder. En un kiosco/handheld
   x11 dedicado sí tiene sentido forzarlo en el launcher.

## Plan de medición (pendiente, en device)

No medimos todavía en ROCKNIX; esto es lo que hay que correr en la RG351V/RK
device para decidir con datos:

```sh
# 1) Qué backend termina usando SDL (y si hay pulse corriendo).
pgrep -a pulseaudio || echo "sin pulseaudio"
SDL_AUDIODRIVER=alsa   # forzar ALSA
# SDL_AUDIODRIVER=pulseaudio   # comparar

# 2) CPU del proceso de Odisea en una escena con música continua y en una
#    silenciosa, con el mismo tramo: 30 s cada una.
#    Anotar user+sys total y, si se puede, el thread de audio:
top -H -p "$(pgrep -f godot.box3d.frt.arm64)"
# o, sin top:
cat /proc/$(pgrep -f godot.box3d.frt.arm64)/stat

# 3) Descartar xruns/glitches auditivos al subir output_latency a 30/50 ms.
```

Criterio: adoptar `SDL_AUDIODRIVER=alsa` sólo si baja CPU medible y no agrega
xruns ni glitches; el mute por silencio y el `output_latency` se pueden adoptar
por separado sin tocar el driver.

## Evidencia local (x11, no handheld)

- `audio_drivers=3` (Dummy/Pulse/ALSA) y `m31_audio_mute_api` en PASS con
  `--audio-driver ALSA` y con `--audio-driver PulseAudio`.
- FRT handheld no incluye los drivers ALSA/Pulse de Godot; su audio es
  `AudioDriverSDL2` (código citado arriba).
- Sin medición en device todavía: por eso el paso 2 del plan es bloqueante para
  cambiar el launcher.