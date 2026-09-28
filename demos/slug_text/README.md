# Slug Text Demo

Proyecto mínimo y autocontenido para ver `SlugLabel3D` (módulo `modules/slug`
del fork; spec en `docs/slug-text-spec.md`): texto 3D vectorial calculado en el
fragment shader, nítido a cualquier escala, rotación y perspectiva, sin atlas
ni SDF. Todo se arma por código en `demo.gd`; el `.tscn` es sólo el root.

Qué muestra: un título en serif, una prueba de winding (huecos de letras como
`O B 8 @ a e`), texto Latin-1 (`Ñandú, pingüino, ¿qué?`), un bloque estilo
terminal multilínea rotado, un cartel en perspectiva rasante, una etiqueta
billboard, una etiqueta animada (gira y pulsa el alpha) y, para comparar, un
`Label3D` normal con `DynamicFont`. El HUD de arriba a la izquierda muestra los
FPS y, por cada `SlugFont`, la cantidad de glifos, el tiempo de build en ms y
el máximo de curvas por banda (se refresca una vez por segundo).

## Correrlo

Bajar las fuentes Noto (la carpeta `fonts/` se ignora en git; el script es
idempotente):

```bash
demos/slug_text/fetch_fonts.sh
```

Con el binario del fork (necesita GLES3):

```bash
godot.x11.opt.tools.64 --path demos/slug_text
```

Chequeo headless: construye los dos `SlugFont`, verifica `is_valid()` y más de
180 glifos, y arma un `SlugLabel3D` con todo el rango ASCII + Latin-1:

```bash
godot.x11.opt.tools.64 --no-window --path demos/slug_text -s check.gd
# imprime SLUG_OK y sale con 0
```

## Controles

| Tecla | Qué hace |
|-------|----------|
| `Espacio` | pausa/reanuda la órbita de la cámara |
| `1` / `2` / `3` | radio de órbita 4 / 9 / 20 |

La cámara orbita el origen a altura 2.5 mirando a `(0, 1.5, 0)`.
