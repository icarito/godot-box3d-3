extends Spatial
# Demo vectorial de Slug (fase 1: modelo de forma genérico + cúbicas CFF/OTF).
# El .tscn es sólo un Spatial; la escena se arma por código.
#
# Muestra lo que la técnica hace bien: texto/curvas exactas por fragmento, sin
# atlas ni SDF, nítidas a cualquier escala, rotación y perspectiva. La misma
# fuente CFF (cúbica) se compara contra un Label3D rasterizado (DynamicFont).
#
# Controles:
#   Espacio    pausa/reanuda la órbita
#   1 / 2 / 3  radio de órbita 4 / 9 / 20
#   Z          microscopio: primer plano de la comparación (vs Label3D)
#   F          cambia la fuente de la línea grande (CFF <-> TTF)
#   + / -      agrega/quita copias de estrés (mide costo por cantidad)
#   Esc        sale

const ORBIT_HEIGHT := 2.6
const LOOK_TARGET := Vector3(0, 1.6, 0)
const ORBIT_SPEED := 0.35
const ORBIT_RADII := {KEY_1: 4.0, KEY_2: 9.0, KEY_3: 20.0}
const COMPARE_TEXT := "Hamburgefonstiv 0123"
const SHOWCASE_TEXT := "O B 8 @ a e & g Q %  ¡Ñandú!"

var camera: Camera
var orbit_radius := 9.0
var orbit_angle := 0.0
var orbit_paused := false
var microscope := false

var cff_font: SlugFont
var ttf_font: SlugFont
var cff_data: DynamicFontData
var ttf_data: DynamicFontData
var svg_vector: SlugVector
var sugar_vector: SlugVector

var big_label: SlugLabel3D
var compare_root: Spatial
var compare_slug: SlugLabel3D
var compare_dyn: Label3D
var spinning: SlugLabel3D
var stress_root: Spatial
var icon_big: SlugVector3D
var icon_grazing: SlugVector3D
var sugar_a: SlugVector3D
var sugar_b: SlugVector3D

# Paletas estilo Sugar (XO): [fill, stroke].
const PALETTES := [
	[Color(1.0, 0.56, 0.0), Color(0.0, 0.41, 0.36)],
	[Color(0.85, 0.11, 0.38), Color(0.10, 0.14, 0.49)],
	[Color(0.26, 0.63, 0.28), Color(0.20, 0.41, 0.12)],
	[Color(0.12, 0.53, 0.90), Color(0.05, 0.28, 0.63)],
	[Color(0.56, 0.14, 0.67), Color(0.29, 0.08, 0.55)],
]
var palette_index := 0

var stats_label: Label
var controls_label: Label
var slug_fonts := []
var stress_copies := 0

var _time := 0.0
var _stats_accum := 0.0
var _palette_accum := 0.0


func _ready():
	_setup_environment()
	_setup_floor()
	_setup_light()
	_setup_camera()

	cff_data = DynamicFontData.new()
	cff_data.font_path = "res://fonts/SourceSans3-Regular.otf"
	ttf_data = DynamicFontData.new()
	ttf_data.font_path = "res://fonts/NotoSans-Regular.ttf"

	cff_font = SlugFont.new()
	cff_font.font_data = cff_data
	ttf_font = SlugFont.new()
	ttf_font.font_data = ttf_data
	slug_fonts = [["CFF/OTF", cff_font], ["TTF", ttf_font]]

	svg_vector = SlugVector.new()
	svg_vector.svg_path = "res://assets/hud.svg"

	sugar_vector = SlugVector.new()
	sugar_vector.svg_path = "res://assets/sugar_icon.svg"

	if not cff_font.is_valid() or not ttf_font.is_valid():
		push_error("SlugFont inválido: faltan las fuentes. Corré demos/slug_vector/fetch_fonts.sh")
		_show_missing_fonts()
	else:
		if not svg_vector.is_valid():
			push_error("SlugVector inválido: falta demos/slug_vector/assets/hud.svg")
		if not sugar_vector.is_valid():
			push_error("SlugVector inválido: falta demos/slug_vector/assets/sugar_icon.svg")
		_apply_palette()
		_build_showcase()

	_setup_hud()


func _setup_environment():
	var world_env := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.02, 0.02, 0.035)
	env.ambient_light_color = Color(0.2, 0.2, 0.25)
	env.ambient_light_energy = 1.0
	world_env.environment = env
	add_child(world_env)


func _setup_floor():
	var mesh := MeshInstance.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(40, 40)
	mesh.mesh = plane
	var mat := SpatialMaterial.new()
	mat.albedo_color = Color(0.06, 0.06, 0.08)
	mat.roughness = 0.9
	mesh.material_override = mat
	add_child(mesh)


func _setup_light():
	var light := DirectionalLight.new()
	light.rotation_degrees = Vector3(-50, -30, 0)
	add_child(light)


func _setup_camera():
	camera = Camera.new()
	add_child(camera)
	_update_camera()


func _update_camera():
	if microscope:
		camera.translation = Vector3(0.6, 1.55, -1.6)
		camera.look_at(Vector3(0, 1.5, -3), Vector3.UP)
		return
	camera.translation = Vector3(sin(orbit_angle) * orbit_radius, ORBIT_HEIGHT, cos(orbit_angle) * orbit_radius)
	camera.look_at(LOOK_TARGET, Vector3.UP)


func _show_missing_fonts():
	var canvas := CanvasLayer.new()
	add_child(canvas)
	var label := Label.new()
	label.text = "Faltan las fuentes.\nCorré demos/slug_vector/fetch_fonts.sh"
	label.rect_position = Vector2(400, 320)
	canvas.add_child(label)


func _build_showcase():
	# a) título grande, serif CFF
	big_label = _make_slug("Slug vectorial", cff_font, 1.1, Vector3(0, 3.6, -4.5), Color(1, 0.95, 0.85))
	big_label.rotation_degrees.y = -14

	# b) comparación 1:1 Slug vs Label3D rasterizado, mismo tamaño de mundo
	compare_root = Spatial.new()
	compare_root.translation = Vector3(-1.7, 2.0, -2.5)
	add_child(compare_root)

	compare_slug = SlugLabel3D.new()
	compare_slug.text = COMPARE_TEXT
	compare_slug.font = cff_font
	compare_slug.size = 0.55
	compare_slug.align = SlugLabel3D.ALIGN_LEFT
	compare_slug.modulate = Color(1, 1, 1)
	compare_root.add_child(compare_slug)

	compare_dyn = Label3D.new()
	compare_dyn.text = COMPARE_TEXT
	var dyn := DynamicFont.new()
	dyn.font_data = cff_data
	dyn.size = 96
	compare_dyn.font = dyn
	compare_dyn.pixel_size = 0.55 / 96.0 # mismo alto de em que el Slug de arriba
	compare_dyn.horizontal_alignment = Label3D.ALIGN_LEFT
	compare_dyn.translation = Vector3(0, -0.7, 0)
	compare_dyn.modulate = Color(1, 0.55, 0.55)
	compare_root.add_child(compare_dyn)

	# c) prueba de winding (huecos) con la TTF
	_make_slug(SHOWCASE_TEXT, ttf_font, 0.42, Vector3(0, 0.5, -3), Color(1, 1, 1))

	# d) HUD diegético en perspectiva rasante: multilínea, alineado, rotado
	var hud_panel := _make_slug("> ship status: NOMINAL\n> hull: 100%\n> slug: GPU", cff_font, 0.28, Vector3(-4.2, 1.4, -1), Color(0.25, 1, 0.35))
	hud_panel.align = SlugLabel3D.ALIGN_LEFT
	hud_panel.rotation_degrees = Vector3(-72, 35, 0)

	# e) billboard
	var billboard := _make_slug("billboard", cff_font, 0.4, Vector3(3.6, 2.4, 1), Color(1, 0.65, 0.15))
	billboard.billboard = true

	# f) animado
	spinning = _make_slug("girando", cff_font, 1.0, Vector3(3.6, 1.0, -1), Color(0.6, 0.8, 1, 1))

	# g) SVG: un icono vectorial grande girando, y uno en perspectiva rasante
	icon_big = _make_icon(2.2, Vector3(1.8, 2.5, 1.2))
	icon_grazing = _make_icon(1.4, Vector3(4.2, 0.35, -1))
	icon_grazing.rotation_degrees = Vector3(-78, 20, 0)

	# h) SVG estilo Sugar: relleno y contorno como roles dinámicos que se recolorean
	sugar_a = _make_sugar_icon(1.9, Vector3(-2.9, 2.2, 0.6), Vector3(0, 18, 0))
	sugar_b = _make_sugar_icon(1.2, Vector3(-2.9, 0.4, 1.2), Vector3(0, -22, 0))

	stress_root = Spatial.new()
	add_child(stress_root)


func _make_sugar_icon(size: float, pos: Vector3, rot_deg: Vector3) -> SlugVector3D:
	var icon := SlugVector3D.new()
	icon.vector = sugar_vector
	icon.size = size
	icon.translation = pos
	icon.rotation_degrees = rot_deg
	add_child(icon)
	return icon


func _apply_palette():
	if sugar_vector == null:
		return
	var palette = PALETTES[palette_index]
	sugar_vector.fill_color = palette[0]
	sugar_vector.stroke_color = palette[1]


func _make_icon(size: float, pos: Vector3) -> SlugVector3D:
	var icon := SlugVector3D.new()
	icon.vector = svg_vector
	icon.size = size
	icon.translation = pos
	add_child(icon)
	return icon


func _make_slug(text: String, font: SlugFont, size: float, pos: Vector3, color: Color) -> SlugLabel3D:
	var label := SlugLabel3D.new()
	label.text = text
	label.font = font
	label.size = size
	label.translation = pos
	label.modulate = color
	add_child(label)
	return label


func _rebuild_stress():
	for child in stress_root.get_children():
		child.queue_free()
	var per_row := 5
	for i in range(stress_copies):
		var label := _make_slug("HOLA", cff_font, 0.5, Vector3.ZERO, Color(0.85, 0.85, 0.95))
		label.get_parent().remove_child(label)
		stress_root.add_child(label)
		var col := i % per_row
		var row := int(i / per_row)
		label.translation = Vector3(-2.2 + col * 1.1, 0.3 + row * 0.55, 2.5)


func _setup_hud():
	var canvas := CanvasLayer.new()
	add_child(canvas)
	stats_label = Label.new()
	stats_label.rect_position = Vector2(12, 10)
	canvas.add_child(stats_label)
	controls_label = Label.new()
	controls_label.rect_position = Vector2(12, 560)
	controls_label.text = "Espacio: órbita  |  1/2/3: radio  |  Z: microscopio  |  F: CFF/TTF  |  C: paleta  |  +/-: estrés  |  Esc: salir"
	canvas.add_child(controls_label)
	_refresh_stats()


func _refresh_stats():
	if not stats_label:
		return
	var text := "FPS: %d  |  etiquetas: %d (+%d estrés)\n" % [
		Engine.get_frames_per_second(), _count_slug_labels(), stress_copies
	]
	for entry in slug_fonts:
		var font: SlugFont = entry[1]
		if font.is_valid():
			text += "%s: %d glifos, %.2f ms build, %d curvas/banda máx\n" % [
				entry[0], font.get_glyph_count(),
				font.get_build_time_usec() / 1000.0,
				font.get_max_curves_per_band()
			]
		else:
			text += "%s: inválido\n" % entry[0]
	if svg_vector:
		if svg_vector.is_valid():
			text += "SVG: %d formas, %.2f ms build, %d curvas/banda máx\n" % [
				svg_vector.get_shape_count(),
				svg_vector.get_build_time_usec() / 1000.0,
				svg_vector.get_max_curves_per_band()
			]
		else:
			text += "SVG: inválido\n"
	if sugar_vector:
		if sugar_vector.is_valid():
			text += "SVG Sugar: %d formas, %d curvas/banda máx, paleta %d/%d\n" % [
				sugar_vector.get_shape_count(),
				sugar_vector.get_max_curves_per_band(),
				palette_index + 1, PALETTES.size()
			]
		else:
			text += "SVG Sugar: inválido\n"
	if microscope:
		text += "MICROSCOPIO: Slug (arriba) vs Label3D rasterizado (abajo)\n"
	stats_label.text = text


func _count_slug_labels() -> int:
	var n := 0
	for child in get_children():
		if child is SlugLabel3D:
			n += 1
	return n


func _process(delta):
	_time += delta
	if not orbit_paused and not microscope:
		orbit_angle += delta * ORBIT_SPEED
		_update_camera()
	if spinning:
		spinning.rotation_degrees.y += delta * 60.0
		var m = spinning.modulate
		spinning.modulate = Color(m.r, m.g, m.b, 0.65 + 0.35 * sin(_time * 2.5))
	if icon_big:
		icon_big.rotation_degrees.y += delta * 30.0
	if sugar_a:
		sugar_a.rotation_degrees.y += delta * 10.0
	_palette_accum += delta
	if _palette_accum >= 1.8:
		_palette_accum = 0.0
		palette_index = (palette_index + 1) % PALETTES.size()
		_apply_palette()
	_stats_accum += delta
	if _stats_accum >= 1.0:
		_stats_accum = 0.0
		_refresh_stats()


func _input(event):
	if not (event is InputEventKey) or not event.pressed or event.echo:
		return
	match event.scancode:
		KEY_SPACE:
			orbit_paused = not orbit_paused
		KEY_1, KEY_2, KEY_3:
			orbit_radius = ORBIT_RADII[event.scancode]
			microscope = false
			_update_camera()
		KEY_Z:
			microscope = not microscope
			_update_camera()
		KEY_F:
			if big_label:
				big_label.font = ttf_font if big_label.font == cff_font else cff_font
		KEY_C:
			palette_index = (palette_index + 1) % PALETTES.size()
			_apply_palette()
			_refresh_stats()
		KEY_EQUAL, KEY_KP_ADD:
			stress_copies = min(stress_copies + 5, 60)
			_rebuild_stress()
			_refresh_stats()
		KEY_MINUS, KEY_KP_SUBTRACT:
			stress_copies = max(stress_copies - 5, 0)
			_rebuild_stress()
			_refresh_stats()
		KEY_ESCAPE:
			get_tree().quit()
