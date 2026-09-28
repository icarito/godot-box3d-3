# Slug Text Demo

Proyecto mínimo y autocontenido para ver `SlugLabel3D` (módulo `modules/slug`
del fork, ver `docs/slug-text-spec.md`): texto 3D vectorial, nítido a
cualquier escala y ángulo, sin atlas ni SDF.

Muestra: título en serif, prueba de winding (huecos de letras como `O B 8 @`),
texto Latin-1, un bloque estilo terminal multilínea rotado, un cartel en
perspectiva rasante, una etiqueta billboard, una etiqueta animada (gira y
pulsa el alpha) y, como comparación, un `Label3D` con `DynamicFont` normal.
Un HUD arriba a la izquierda muestra FPS y, por cada fuente, glifos, tiempo
de build y curvas por banda máximas.

## Correrlo

Primero bajar las fuentes (se ignoran en git):

```bash
demos/slug_text/fetch_fonts.sh
```

Con el binario del fork (necesita GLES3):

```bash
godot.x11.opt.tools.64 --path demos/slug_text
```

Chequeo headless (arma los `SlugFont` y un `SlugLabel3D` con todo el rango
ASCII + Latin-1):

```bash
godot.x11.opt.tools.64 --no-window --path demos/slug_text -s check.gd
```

## Controles

| Tecla | Qué hace |
|-------|----------|
| `Espacio` | pausa/reanuda la órbita de la cámara |
| `1` / `2` / `3` | radio de órbita 4 / 9 / 20 |
