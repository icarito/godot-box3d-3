# Spec: renderer vectorial nativo (SVG) sobre Slug — fase 1

Objetivo: un renderer nativo de SVG/vectores eficiente en el fork de Godot 3.6,
reusando el runtime Slug que ya existe (`modules/slug`, GLES3) y el algoritmo de
Lengyel. Complementa `docs/slug-text-spec.md` (texto 3D), no lo reemplaza.

**Estado: fases 1 y 2 hechas.** Patches
`zzzzzzzzzzzz_feature_slug_shape_builder` (modelo de forma, builder,
decomposer cúbico) y `zzzzzzzzzzzzz_feature_slug_svg` (SVG nativo:
`SlugVector` + `SlugVector3D`) en la rama `box3d-3.6` de `icarito/godot`. Los
`patches/*.patch` se exportan de esa rama.

## 1. Por qué

El port actual (`slug_font.cpp`) tenía el empaquetado de curvas/bandas y el
shader acoplados a FreeType, y rechazaba contornos cúbicos (CFF/OTF). El
objetivo es que cualquier productor de curvas cuadráticas —una fuente, un path
SVG, un icono— alimente **el mismo** empaquetado y **el mismo** shader
(`slug_shader.h`). Sin eso, un renderer SVG obligaría a duplicar bandas, texturas
y material.

La fase 1 es el refactor que abre ese camino y, de paso, habilita cúbicas.

## 2. Qué hizo la fase 1

| Cambio | Dónde |
|---|---|
| Modelo de forma genérico: `SlugQCurve` / `SlugContour` / `SlugShape` (+ `has_curves()`, `compute_bounds()`) | `slug_shape.h` |
| Empaquetado extraído de `SlugFont` a `SlugShapeBuilder::build(shapes) -> SlugAtlasData` (curvas 4096-wide, bandas, `glyph_tex`, texturas float, material compartido) | `slug_shape_builder.{h,cpp}` |
| `SlugCurveDecomposer`: `move_to/line_to/quad_to/cubic_to/close`; cúbica→cuadrática adaptativa por De Casteljau (tolerancia `1/4096` unidades de forma, profundidad máx. 8). Puerto de `CurveDecomposer` de SlugHorn (MIT) | `slug_decomposer.{h,cpp}` |
| `SlugFont` reducido a productor: FreeType → `SlugShape` → builder; mapea codepoint→índice de fila | `slug_font.{h,cpp}` |
| **CFF/OTF habilitado**: `cubic_to` ya no aborta | `slug_font.cpp` |
| Demo: descarga de un OTF CFF (`SourceSans3-Regular.otf`) y chequeo headless que lo construye | `demos/slug_text/{fetch_fonts.sh,check.gd}` |

La API pública de `SlugFont` (glyphs, kerning, métricas, `get_material_rid`) y
`SlugLabel3D` no cambian. El contrato del shader tampoco: una forma = una fila
de `glyph_tex`, índice por `UV2.x`.

## 2b. Fase 2 — SVG nativo (hecha en el patch `zzzzzzzzzzzzz_feature_slug_svg`)

| Cambio | Dónde |
|---|---|
| `slug_parse_svg()`: SVG → `Vector<SlugShape>` + colores por forma, con **NanoSVG**. Reutiliza el `thirdparty/nanosvg/nanosvg.cc` que el motor ya compila para `modules/svg` (no se vendoriza una copia): se incluye solo la declaración y se enlaza ese objeto | `slug_svg.{h,cpp}` |
| Cada elemento con relleno sólido = **un** `SlugShape`; sus subpaths (agujeros) son contornos del mismo shape, así el winding nonzero los cancela como los escribió el autor. Y se invierte para quedar y-up | `slug_svg.cpp` |
| `SlugVector : Resource`: `svg_path`, parseo + `SlugShapeBuilder` lazy, `get_bounds/get_shape_count/is_valid/...` | `slug_vector.{h,cpp}` |
| `SlugVector3D : GeometryInstance`: un quad por forma, con el shader de Slug; `size` = alto en unidades de mundo, color por forma × `modulate`, `billboard` | `slug_vector_3d.{h,cpp}` |
| Registro de las dos clases, `config.py`, doc_classes, y `can_build` que falla si se desactiva el módulo `svg` (de él sale el símbolo `nsvgParse`) | `register_types.cpp`, `config.py` |
| Demo y chequeo: `demos/slug_vector/` (icono SVG, comparación CFF/TTF vs rasterizado, bench) | repo |

**Hallazgo:** `SlugCurveDecomposer::FLATNESS` (1/4096) está calibrado en unidades
de em (~1). Los SVG vienen en unidades de usuario (decenas o cientos), y con esa
tolerancia absoluta las cúbicas se subdividían sin control: un icono de 26
cúbicas generaba ~8300 quads y el bloque de bandas superaba el ancho de textura
(4096). El parser **normaliza el documento a ~1 unidad** (`1 / max(width,height)`
del viewBox) antes de descomponer; `SlugVector3D.size` reescala igual, así que es
invisible para el usuario. El icono del demo quedó en 56 curvas/banda y 0.33 ms
de build.

**Rendimiento** (llvmpipe software, 1280×720; no es GPU real, sirve para
comparar): build SVG 0.33 ms; frame de 16 iconos chicos ~18 ms, 16 grandes
~45 ms, 1 grande ~9 ms. El costo es por píxel cubierto × curvas/banda. Detalle en
`demos/slug_vector/README.md`.

**Límites de esta fase:** sin strokes (stroke-to-fill), sin gradientes (se
saltean), `fill-rule:evenodd` no soportado (el shader es nonzero), sin nodo 2D
(`SlugVector3D` sólo), y sin import a `.res` (el SVG se lee crudo en runtime).

## 3. Verificación

Binario: `platform=x11 target=release_debug tools=yes production=yes`, renderer
GLES3 sobre llvmpipe. `demos/slug_text/check.gd` (`SLUG_OK`, exit 0):

| Fuente | Glifos | Build (mediana de 3) | Curvas/banda máx |
|---|---|---|---|
| NotoSans-Regular.ttf (TrueType) | 192 | ~14 ms | **16** |
| NotoSerif-Regular.ttf (TrueType) | 192 | ~16 ms | **19** |
| SourceSans3-Regular.otf (CFF) | 191 | ~70 ms | **140** |

El máximo de curvas/banda de las TrueType es idéntico al baseline del spec de
texto (16 y 19): el refactor **no cambia la salida** para cuadráticas. Antes de
esta fase, la CFF era rechazada ("cubic (CFF/OTF) outlines are not supported").

## 4. Hallazgo: el tope de bandas y el costo de las cúbicas

`SLUG_MAX_BANDS = 16` es el techo de la grilla de bandas por eje. La búsqueda
del reparto prueba 1..16 y es O(bandas² × curvas): subirlo mejora el reparto
pero encarece el preprocesado. Medido con el mismo chequeo:

| `SLUG_MAX_BANDS` | Sans (curvas/banda, build) | CFF (curvas/banda, build) |
|---|---|---|
| 16 (actual) | 16, ~14 ms | 140, ~70 ms |
| 64 | 14, ~79 ms | 68, ~544 ms |

Conclusión: bajar las curvas/banda de CFF subiendo el tope sale caro en build
(544 ms). La palanca correcta no es más bandas uniformes sino **colocación por
densidad + tabla de indirección** (lo que hace SlugHorn), y/o aflojar la
tolerancia del decomposer. Se deja para fase 4, con medición.

Nota de costo: 140 curvas/banda es el peor caso *de la lista*; el shader corta
el barrido por `max-x` descendente en cuanto la curva queda a más de medio píxel,
así que no son 140 evaluaciones por píxel.

## 5. Fases siguientes

- **Fase 2 (resto).** Strokes (stroke-to-fill), gradientes, `fill-rule:evenodd`
  (variante de shader) y un nodo 2D (`SlugVector2D`/`TextureRect`-like) para UI
  plana.
- **Fase 3 — asset importable.** `EditorImportPlugin` `.svg` → recurso
  `SlugAtlas` con los buffers ya empaquetados (bytes crudos de
  `curve`/`band`/`glyph`, porque las texturas son float y no van a PNG), para no
  parsear en runtime ni depender de que el `.svg` crudo se exporte.
- **Fase 4 — empaquetado.** Splits por densidad + indirección (SlugHorn), o
  tope de bandas adaptativo, con la métrica de curvas/banda y ms de GPU.

## 6. Límites y riesgos

- El shader actual asume **grilla de bandas uniforme**; consumir `.slugb` de
  SlugHorn tal cual no es posible sin portar la indirección.
- CFF con la tolerancia actual infla curvas/banda (140); ajustable.
- Godot 3 `Image` no tiene formatos enteros: bandas en `RGF` float (4× el
  `RG16UI` de SlugHorn), igual que el port de texto.
- El bloque de bandas de una forma debe entrar en el ancho de textura (4096);
  `SlugShapeBuilder` falla con error explícito si no.
- SVG es y-down y las fuentes del fork y-up: el frontend de fase 2 debe aplicar
  el flip/origen (SlugHorn lo modela con `ShapeInfo::Origin`).
