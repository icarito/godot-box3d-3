# Glow map demo — lens dirt (PR #93133)

Proyecto mínimo y autocontenido para ver el `glow_map` de Godot 3.7 (backport
del PR upstream [#93133](https://github.com/godotengine/godot/pull/93133)) que
vive en `patches/zzzzz_feature_glow_map_gles3.patch`.

Una sala oscura con barras de neón emisivas y un `Environment` con glow fuerte.
El `glow_map` es una textura que **multiplica** el glow resultante, modulada por
`glow_map_strength`: el "lens dirt" clásico, donde la suciedad de la lente deja
pasar el glow sólo por las manchas. El mapa se genera por código (OpenSimplex +
manchas radiales), así que no hay ningún asset binario que mantener.

Sin mapa el glow es un halo uniforme alrededor de las barras; con el mapa
aparecen las manchas y el halo queda moteado y recortado. El preview
(`preview.gif` barriendo `glow_map_strength` de 0 a 1 y de vuelta, más un
`compare.png` lado a lado) **no se versiona** —este repo guarda sólo texto— y lo
adjunta el nightly como assets del release y lo regenera `preview.gd` (abajo).

## Correrlo

Con el editor/nightly del fork (necesita GLES3):

```bash
godot.box3d.linux.x86_64.editor --path demos/glow_map
```

o desde el editor: *Import* → `demos/glow_map/project.godot` → *Play*.

Controles:

| Tecla | Qué hace |
|-------|----------|
| `G` | prende/apaga el glow |
| `M` | prende/apaga el `glow_map` (el "antes": sin mapa el glow es uniforme) |
| `[` / `]` | baja/sube `glow_map_strength` |
| `R` | vuelve a los valores iniciales |
| `Esc` | sale |

El glow map es sólo GLES3. Con `--video-driver GLES2` el motor ignora el mapa
(el `environment_set_glow_map` de ese rasterizador es un no-op documentado), así
que con `M` apagado y encendido se ve igual.

## Regenerar el preview

`preview.gd` barre la fuerza sobre la escena y deja los frames y los dos stills:

```bash
GLOW_DEMO_OUT=/tmp/glow_frames xvfb-run -a -s "-screen 0 1024x600x24" \
  godot.box3d.linux.x86_64.editor --path demos/glow_map \
  --video-driver GLES3 -s preview.gd
```

con el binario del fork (`../godot/bin/godot.x11.opt.tools.64`). Después:

```bash
ffmpeg -y -framerate 24 -i /tmp/glow_frames/frame_%04d.png \
  -vf "scale=640:-1,palettegen=stats_mode=diff" /tmp/palette.png
ffmpeg -y -framerate 24 -i /tmp/glow_frames/frame_%04d.png -i /tmp/palette.png \
  -lavfi "scale=640:-1[x];[x][1:v]paletteuse" media/preview.gif
cp /tmp/glow_frames/map_off.png /tmp/glow_frames/map_on.png media/
```