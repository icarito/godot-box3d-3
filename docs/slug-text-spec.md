# Spec: texto 3D con Slug (GLES3) en el fork de Godot 3.6

**ESTADO: POC de fase B funcionando.** Módulo `modules/slug` en la rama
`box3d-3.6` de `icarito/godot` (export: `patches/zzzzzzzz_feature_slug_text.patch`).
Demo: `demos/slug_text/`.

Medido 2026-09-28 (NotoSans Regular, TrueType):
- Preprocesado en runtime: 192 glifos (ASCII + Latin-1 + U+FFFD) en **~5 ms**,
  máximo **16 curvas por banda**. Import-time no se justifica por ahora.
- Imagen correcta a la primera en **Iris Xe (Mesa)** y en **llvmpipe**
  (idénticas): huecos de `O B 8 @ a e`, perspectiva rasante y zoom extremo
  (borde de curva limpio a cientos de píxeles por em). El riesgo de `fwidth`
  en software queda descartado para llvmpipe.
- Única corrección al port: el tokenizador de 3.6 rechaza el sufijo `u` en
  literales hex (`0x2E74u`); se usan decimales.

Enriquece el pre-research original (texto pegado en la sesión del
2026-09-28) con lo verificado en la fuente primaria y con Perplexity. Lo
marcado *[no verificado]* viene de Perplexity sin fuente primaria que lo
respalde.

## 1. Qué es y por qué

Slug (Eric Lengyel, JCGT 2017) dibuja cada glifo como un quad y calcula en el
fragment shader la cobertura exacta del contorno a partir de sus curvas
Bézier cuadráticas: sin atlas rasterizado, sin SDF/MSDF. Nitidez a cualquier
escala, rotación y perspectiva; el costo es aritmética por píxel, proporcional
a las curvas que caen en la *banda* del píxel.

Complementa, no reemplaza: DynamicFont/Label3D siguen siendo lo correcto para
UI, menús, subtítulos y texto denso chico. Slug es el camino premium para
carteles grandes, terminales cercanas, UI diegética/holográfica, títulos
animados.

## 2. Licencia y atribución (verificado)

- `github.com/EricLengyel/Slug`: `SlugPixelShader.hlsl` y
  `SlugVertexShader.hlsl`, doble licencia **MIT o Apache-2.0**
  (`LICENSE`, `NOTICE`: "Slug shader code Copyright 2017 by Eric Lengyel").
- La patente US 10,373,352 fue **dedicada al dominio público** el
  2026-03-17 (terminal disclaimer; terathon.com/blog/decade-slug.html).
- El README exige **dar crédito** si se distribuye: el shader portado lleva el
  copyright en el encabezado y el módulo agrega una entrada en
  `COPYRIGHT.txt`/`thirdparty` equivalente (`modules/slug/SLUG_LICENSE.txt`).

## 3. Lo que dice la fuente primaria (reglas que el port respeta)

Del README y los comentarios del shader de referencia:

1. **Curvas**: textura de 4 canales; un texel = (p1, p2) de una cuadrática,
   p3 está en `.xy` del texel siguiente. En un contorno, el texel "segundo" de
   una curva es el "primero" de la siguiente (comparten extremo). Una curva no
   puede cruzar el borde de fila de la textura (se lee `x+1`).
2. **Rectas** como cuadrática `{p1, p2, p2}` (duplicar el segundo extremo).
3. **Bandas**: una glifo tiene `nh` bandas horizontales y `nv` verticales del
   mismo grosor. Cabeceras de banda (cantidad, offset) en la textura de
   bandas a partir de `glyphLoc`: primero las `nh` horizontales, después las
   `nv` verticales; luego las listas de ubicaciones de curva. Las listas
   pueden envolver a la fila siguiente (`CalcBandLoc`, ancho 4096); las
   cabeceras no.
4. Asignación a bandas con **epsilon 1/1024 em** (bandas levemente solapadas).
   Curvas **ordenadas por max-x descendente** (horizontales) / **max-y
   descendente** (verticales): el loop corta en cuanto la curva queda a más de
   medio píxel detrás.
5. Rectas **horizontales nunca** en bandas horizontales; **verticales nunca**
   en verticales.
6. `nh`, `nv` se eligen para **minimizar el máximo de curvas por banda**.
   Bandas con el mismo conjunto pueden apuntar a los mismos datos
   (optimización opcional, fase D).
7. Cobertura: rayos horizontal y vertical, `CalcRootCode` (signos de y1,y2,y3
   → tabla 0x2E74), raíces en unidades de píxel vía `fwidth(renderCoord)`,
   combinación ponderada; `abs()` hace que funcione con cualquier sentido de
   giro (TrueType es horario). Regla nonzero por defecto; even-odd y
   `SLUG_WEIGHT` (sqrt para engrosar) son opcionales.
8. **Dilatación dinámica** en vertex: cada vértice del quad se corre sobre su
   normal de esquina lo justo para agregar ~medio píxel en pantalla, dado MVP
   y viewport; el texcoord em se corrige con la **Jacobiana inversa**
   (em por unidad de objeto). Evita tanto el recorte del antialiasing como
   pagar un margen fijo de fragmentos.

## 4. Decisiones del port a Godot 3.6

| Tema | Decisión | Por qué |
|---|---|---|
| Dónde | Módulo `modules/slug` en el fork, sin tocar el rasterizador | Todo lo que el algoritmo necesita existe en el lenguaje de shaders de 3.6 (verificado en `servers/visual/shader_language.cpp` / `shader_types.cpp`): `texelFetch`, `floatBitsToUint`, operaciones de bits en `uint`, `fwidth`, varyings `flat`, `VIEWPORT_SIZE`/`PROJECTION_MATRIX`/`MODELVIEW_MATRIX` en `vertex()`. Un `ShaderMaterial` interno hereda niebla, orden de transparencia, profundidad y culling del pipeline normal. Se apaga con `module_slug_enabled=no`. |
| Formatos | Curvas `Image::FORMAT_RGBAF` (float32), bandas `FORMAT_RGF` (float32 con enteros exactos), filtro off | `Image` de Godot 3 no tiene formatos enteros, así que el `RG16UI` de la referencia no es expresable. Float32 representa enteros exactos hasta 2^24 (el three.js addon hace lo mismo con WebGL2 *[no verificado]*). Half-float para curvas queda para la fase D, cuando haya comparación visual. |
| Datos por glifo | Textura chica `glyph_tex` (2 texels RGBAF por glifo: transform de bandas; glyphLoc + bandMax). El vértice lleva solo el índice de glifo | Godot 3 no tiene atributos custom; `UV2`/`COLOR`/`NORMAL` sin compresión alcanzan si lo per-glifo vive en textura. Se lee en `vertex()` (VTF en GLES3) y viaja como varying `flat`. |
| Atributos | `VERTEX` = esquina en el plano z=0 local; `NORMAL.xy` = dirección de dilatación, `NORMAL.z` = flag billboard; `UV` = em del glifo; `UV2` = (índice de glifo, em por unidad local); `COLOR` = color. Superficie con `compress_flags = 0` | Todo lo per-label va en el vértice, así **un único material por fuente** sirve a todas las labels. |
| Jacobiana | Diagonal `em_per_unit` (UV2.y): el label no deforma, el nodo aplica rotación/escala/perspectiva por MVP | Exacto para quads alineados; la escala del nodo entra por MVP, que es lo que usa la dilatación. |
| Carga de fuente | `SlugFont.font_data: DynamicFontData`; FreeType ya integrado (`FT_Load_Glyph` con `FT_LOAD_NO_SCALE|NO_HINTING`, `FT_Outline_Decompose`) | Reusa el importador de .ttf existente y el mismo archivo funciona en export. FreeType ya resuelve los puntos on-curve implícitos de TrueType en `conic_to` (la respuesta de Perplexity decía lo contrario: incorrecto). |
| Cuándo se preprocesa | **Al primer uso, en runtime**, y se mide. El spec original pedía import-time | ASCII + Latin-1 son ~190 glifos con decenas de curvas: se espera ~ms. Si la medición lo contradice, fase C agrega `ResourceFormatSaver`/import. |
| Cúbicas (CFF/OTF) | Se rechazan con error claro y la fuente queda inválida | Spec original. Fase D: subdivisión cúbica→cuadrática con tolerancia en em. |
| Charset | U+0020–U+007E, U+00A0–U+00FF, U+FFFD. Faltante → U+FFFD o `?`, con `WARN_PRINT_ONCE` | Spec original. |
| Layout | `\n`, avance, kerning `FT_Get_Kerning(FT_KERNING_UNSCALED)` (tabla `kern` legacy), alineación izq/centro/der, `line_spacing`; bloque centrado en el origen como `Label3D` | Sin shaping/BiDi/ligaduras (no-objetivos). GPOS kerning queda afuera. |
| Transparencia | `blend_mix`, `unshaded`, `cull_disabled`, `depth_draw_opaque` | Igual que `Label3D`. `alpha_cut` (profundidad) queda para C: exige un segundo material por fuente. |
| Empaquetado de bandas | El bloque entero de un glifo (cabeceras + listas) va en una fila de 4096 | Con eso el shader lee `glyphLoc.x + offset` sin el `CalcBandLoc` de la referencia; un glifo latino ocupa decenas de texels. |
| AABB | Custom AABB del mesh; con billboard se expande a la esfera que lo contiene | La dilatación agrega ≤1 px; el billboard gira fuera del AABB local. |
| GLES2 / capacidades | En GLES2 el nodo no dibuja y avisa una vez. Fallback a `Label3D` = fase C | El shader no compila en GLES2 (`floatBitsToUint`, `texelFetch`). Falla visible y segura, nunca basura. |

## 5. API (MVP)

```text
SlugFont : Resource
    font_data : DynamicFontData      # .ttf
    is_valid() -> bool               # construye si hace falta
    get_glyph_count() -> int
    get_build_time_usec() -> int     # para medir la decisión runtime vs import

SlugLabel3D : GeometryInstance
    text : String (multiline)
    font : SlugFont
    size : float = 1.0               # unidades de mundo por em
    modulate : Color = white
    align : ALIGN_LEFT | ALIGN_CENTER | ALIGN_RIGHT
    line_spacing : float = 0.0       # em extra entre líneas
    billboard : bool = false
```

## 6. Plan por fases

**A. Prototipo del shader** (hecho dentro de la fase B, por costo): port 1:1
de `SlugRender` y `SlugDilate` a GLSL de Godot. Validación visual con glifos
de huecos (`O B 8 @ a e`), rotación y perspectiva extrema.

**B. Prueba de módulo (este POC)**
1. `modules/slug`: `config.py`, `SCsub` (incluye `thirdparty/freetype/include`
   si `builtin_freetype`), `register_types`.
2. `SlugFont`: FreeType → contornos cuadráticos → bandas → tres texturas +
   `ShaderMaterial` compartido.
3. `SlugLabel3D`: layout → mesh (4 vértices/6 índices por glifo), rebuild
   diferido solo al cambiar texto/fuente/tamaño/alineación.
4. Demo `demos/slug_text/`: carteles grandes, rotados, en perspectiva
   rasante, billboard, animado, y un `Label3D` al lado para comparar.
5. Chequeo headless (`demos/slug_text/check.gd`): la fuente construye, `O`
   tiene 2 contornos, `i` tiene curvas en >1 banda, el tiempo de build.

**C. Usabilidad mínima**
- Fallback: `SlugLabel3D.fallback_font` (DynamicFont) → `Label3D` hijo interno
  cuando el driver no es GLES3 o la fuente no carga.
- Outline simple (segunda pasada con `SLUG_WEIGHT`/dilatación de cobertura).
- Serialización del preprocesado (si la medición de B lo justifica).
- Iconos/documentación de clase (`doc_classes/*.xml`).
- Perfil: Intel Mesa, AMD, NVIDIA, llvmpipe. Métrica: ms de GPU con el
  `zzzzzz_frt_gpu_timer` existente, a 1080p, 10 carteles a pantalla completa.

**D. Optimización (solo con mediciones de C)**
- Curvas en half-float; compartir datos de bandas idénticas/sublistas.
- Batching: un mesh para varias labels de la misma fuente.
- Cúbicas (OTF/CFF), rango Unicode configurable, GPOS.

## 7. Criterios (del pre-research, sin cambios)

Seguir si: calidad claramente superior a `Label3D` en texto grande 3D; costo de
fragmento aceptable para pocos carteles; preprocesado determinista; aislado de
la pila de fuentes/UI; se apaga con opción de build. Parar si el winding es
frágil entre fuentes comunes, si los formatos float fallan en drivers
objetivo, o si MSDF alcanza para el requisito artístico con mucho menos costo.

## 8. Riesgos a vigilar en el POC

- `fwidth` en llvmpipe: la referencia depende de derivadas por píxel para
  convertir em→píxel. Si falla, alternativa analítica a partir de la
  Jacobiana del vértice.
- Precisión: todo float32 en el POC; el umbral "casi lineal" 1/65536 de la
  referencia está en unidades em, igual que nuestros datos.
- Cabeceras de banda que crucen fila: el empaquetador salta de fila antes de
  empezar un glifo que no entra.
