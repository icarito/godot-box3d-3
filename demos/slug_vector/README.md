# Slug Vector Demo

Proyecto mínimo y autocontenido para el **renderer vectorial nativo** del módulo
`modules/slug` del fork (spec en `docs/slug-vector-spec.md`): texto y **SVG**
calculados en el fragment shader con el algoritmo de Slug, sin atlas rasterizado
ni SDF. Nítido a cualquier escala, rotación y perspectiva.

Qué muestra:

- Un SVG (`assets/hud.svg`, autoría propia) dibujado por `SlugVector3D`, con
  **agujeros reales** (marco y anillo con subpaths de winding opuesto),
  triángulo, curva cúbica y una cola lineal, cada forma con su color.
- Un icono **estilo Sugar** (`assets/sugar_icon.svg`): usa las entidades
  `&fill_color;` / `&stroke_color;` (también se soportan los `var(--fill-color)`
  de Sugarizer v2) y **strokes reales**. El demo cicla paletas XO y recolorea
  fill y stroke en runtime (`C`), sin reconstruir la geometría.
- Texto `SlugLabel3D` con una fuente **CFF/OTF** (cúbica) y una TrueType.
- Comparación 1:1 contra un `Label3D` rasterizado (DynamicFont) al mismo tamaño
  de mundo: en el microscopio (`Z`) se ve el escalón/blur del rasterizado.
- HUD diegético en perspectiva rasante, billboard, y un bloque de estrés.

## Correrlo

Fuentes (la carpeta `fonts/` se ignora en git; el script es idempotente). El SVG
ya está versionado en `assets/`:

```bash
demos/slug_vector/fetch_fonts.sh
```

Con el binario del fork (necesita GLES3):

```bash
godot.x11.opt.tools.64 --path demos/slug_vector
```

Chequeo headless (parsea el SVG, verifica formas/curvas y arma un
`SlugVector3D`):

```bash
godot.x11.opt.tools.64 --no-window --path demos/slug_vector -s check.gd
# imprime SLUG_OK y sale con 0
```

Benchmark headless (ms/frame por carga; ver abajo):

```bash
godot.x11.opt.tools.64 --no-window --path demos/slug_vector -s bench.gd
```

## Controles

| Tecla | Qué hace |
|-------|----------|
| `Espacio` | pausa/reanuda la órbita de la cámara |
| `1` / `2` / `3` | radio de órbita 4 / 9 / 20 |
| `Z` | microscopio: primer plano de Slug vs Label3D rasterizado |
| `F` | cambia la fuente de la línea grande (CFF ↔ TTF) |
| `C` | cambia la paleta fill/stroke del icono Sugar (también cicla sola) |
| `+` / `-` | agrega/quita copias de estrés (costo por cantidad) |
| `Esc` | salir |

## Rendimiento / eficiencia

Medición de `bench.gd` en **llvmpipe (software)**, 1280×720. No son números de
GPU real: sirven para comparar cargas y ver el escalado. En GPU el costo por
fragmento baja en órdenes de magnitud.

Preprocesado (una vez, lazy, la primera vez que se usa):

| Fuente | Glifos | Build | Curvas/banda máx |
|---|---|---|---|
| CFF/OTF (Source Sans 3) | 191 | 20.2 ms | 140 |
| TTF (Noto Sans) | 192 | 3.5 ms | 16 |
| SVG (`hud.svg`, 4 formas) | — | 0.33 ms | 56 |
| SVG Sugar (`sugar_icon.svg`, 7 formas con strokes) | — | 0.8 ms | 68 |

Frame:

| Carga | ms/frame | fps |
|---|---|---|
| Slug CFF 1 etiqueta 1em | 7.5 | 133 |
| Slug CFF 10 etiquetas 1em | 17.5 | 57 |
| Slug CFF 40 etiquetas 1em | 41.6 | 24 |
| Slug CFF 1 etiqueta 6em | 13.7 | 73 |
| Slug CFF 8 etiquetas 6em | 103.5 | 9.7 |
| Slug TTF 8 etiquetas 6em | 40.6 | 24.6 |
| Label3D rasterizado 8 etiquetas 6em | 10.5 | 95 |
| Slug SVG 1 icono 2u | 9.1 | 110 |
| Slug SVG 16 iconos 2u | 18.3 | 55 |
| Slug SVG 16 iconos 6u | 45.2 | 22 |
| Slug Sugar 1 icono 2u | 8.7 | 115 |
| Slug Sugar 16 iconos 2u | 24.3 | 41 |

Lectura:

- El costo es **por píxel cubierto × curvas/banda**, no por objeto: 16 iconos
  chicos cuestan casi lo mismo que 1 grande a igual cobertura.
- La CFF cuesta ~2.5× una TTF a igual cobertura (140 vs 16 curvas/banda): la
  tolerancia de subdivisión cúbica es la palanca, y el empaquetado de bandas el
  siguiente objetivo (ver `docs/slug-vector-spec.md`, fases 2 y 4).
- El rasterizado (`Label3D`) es ~5-10× más barato por frame, pero tiene
  resolución fija: se pixela al acercar. Slug paga el precio en fragmento y no
  depende de la resolución de pantalla.
- El SVG (56 curvas/banda) cae entre ambos.
