extends Spatial
# Demo de SlugLabel3D: todo se arma por código, el .tscn es sólo el root.

const ORBIT_RADII = { KEY_1: 4.0, KEY_2: 9.0, KEY_3: 20.0 }

var camera: Camera
var orbit_radius = 9.0
var orbit_angle = 0.0
var orbit_paused = false

var spinning_label: SlugLabel3D
var stats_label: Label
var slug_fonts = [] # Array de [nombre, SlugFont] para el HUD.
var _fallback_font: DynamicFont

var _screenshot_path = ""
var _frame_count = 0


func _ready():
	_setup_environment()
	_setup_floor()
	_setup_light()
	_setup_camera()

	var sans_data = DynamicFontData.new()
	sans_data.font_path = "res://fonts/NotoSans-Regular.ttf"
	var serif_data = DynamicFontData.new()
	serif_data.font_path = "res://fonts/NotoSerif-Regular.ttf"

	var sans_font = SlugFont.new()
	sans_font.font_data = sans_data
	var serif_font = SlugFont.new()
	serif_font.font_data = serif_data

	slug_fonts = [["Sans", sans_font], ["Serif", serif_font]]

	var fallback_font = DynamicFont.new()
	fallback_font.font_data = sans_data
	fallback_font.size = 64
	_fallback_font = fallback_font

	if not sans_font.is_valid() or not serif_font.is_valid():
		push_error("SlugFont inválido: faltan las fuentes. Corré fetch_fonts.sh")
		_show_missing_fonts_label()
	else:
		_build_labels(sans_font, serif_font)

	_setup_hud()
	_parse_cmdline_args()


func _setup_environment():
	var world_env = WorldEnvironment.new()
	var env = Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.03, 0.03, 0.05)
	env.ambient_light_color = Color(0.15, 0.15, 0.18)
	env.ambient_light_energy = 1.0
	world_env.environment = env
	add_child(world_env)


func _setup_floor():
	var floor_mesh = MeshInstance.new()
	var plane = PlaneMesh.new()
	plane.size = Vector2(30, 30)
	floor_mesh.mesh = plane
	var mat = SpatialMaterial.new()
	mat.albedo_color = Color(0.08, 0.08, 0.1)
	mat.roughness = 0.9
	floor_mesh.material_override = mat
	add_child(floor_mesh)


func _setup_light():
	var light = DirectionalLight.new()
	light.rotation_degrees = Vector3(-50, -30, 0)
	light.shadow_enabled = true
	add_child(light)


func _setup_camera():
	camera = Camera.new()
	add_child(camera)
	_update_camera()


func _update_camera():
	var height = 2.5
	camera.translation = Vector3(sin(orbit_angle) * orbit_radius, height, cos(orbit_angle) * orbit_radius)
	camera.look_at(Vector3(0, 2.2, -1), Vector3.UP)


func _show_missing_fonts_label():
	var canvas = CanvasLayer.new()
	add_child(canvas)
	var label = Label.new()
	label.text = "Faltan las fuentes. Corré demos/slug_text/fetch_fonts.sh"
	label.rect_position = Vector2(400, 300)
	canvas.add_child(label)


func _build_labels(sans_font: SlugFont, serif_font: SlugFont):
	# a) título, serif, blanco cálido.
	_make_label("Slug en Godot 3.6", serif_font, 1.2, Vector3(0, 4.4, -3), Color(1, 0.96, 0.9))

	# b) test de winding: huecos de letras.
	_make_label("O B 8 @ a e & g Q %", sans_font, 0.8, Vector3(0, 2.8, -3), Color(1, 1, 1))

	# c) Latin-1.
	_make_label("Ñandú, pingüino, çà, ¿qué? ¡Sí! 1234567890", sans_font, 0.5, Vector3(0, 1.6, -3), Color(1, 1, 1))

	# d) bloque terminal multilínea, alineado a la izquierda, rotado.
	var terminal = _make_label("> status: OK\n> temp: 21.5 C\n> slug: GPU", sans_font, 0.25, Vector3(-4, 1.2, 0), Color(0.2, 1, 0.3))
	terminal.align = SlugLabel3D.ALIGN_LEFT
	terminal.rotation_degrees.y = 60

	# e) perspectiva rasante: casi tirado en el piso.
	var rasante = _make_label("PERSPECTIVA RASANTE", sans_font, 1.0, Vector3(0, 0.02, 2), Color(1, 1, 1))
	rasante.rotation_degrees.x = -85

	# f) billboard.
	var billboard = _make_label("billboard", sans_font, 0.4, Vector3(5, 3, 1), Color(1, 0.6, 0.1))
	billboard.billboard = true

	# g) animado: gira y pulsa el alpha.
	spinning_label = _make_label("girando", sans_font, 0.5, Vector3(5, 1.6, -1), Color(0.6, 0.8, 1, 1))

	# h) comparación con Label3D + DynamicFont normal.
	var dyn_font = DynamicFont.new()
	dyn_font.font_data = sans_font.font_data
	dyn_font.size = 64
	var label3d = Label3D.new()
	label3d.text = "Label3D (DynamicFont)"
	label3d.font = dyn_font
	label3d.pixel_size = 0.005
	label3d.translation = Vector3(0, 0.4, -3)
	add_child(label3d)


func _make_label(text: String, font: SlugFont, size: float, pos: Vector3, color: Color) -> SlugLabel3D:
	var label = SlugLabel3D.new()
	label.text = text
	label.font = font
	label.size = size
	label.translation = pos
	label.modulate = color
	label.fallback_font = _fallback_font
	add_child(label)
	return label


func _setup_hud():
	var canvas = CanvasLayer.new()
	add_child(canvas)
	stats_label = Label.new()
	stats_label.rect_position = Vector2(10, 10)
	canvas.add_child(stats_label)
	_refresh_stats()
	var timer = Timer.new()
	timer.wait_time = 1.0
	timer.autostart = true
	add_child(timer)
	timer.connect("timeout", self, "_refresh_stats")


func _refresh_stats():
	if not stats_label:
		return
	var text = "FPS: %d\n" % Engine.get_frames_per_second()
	for entry in slug_fonts:
		var name = entry[0]
		var font: SlugFont = entry[1]
		if font.is_valid():
			text += "%s: %d glifos, %.2f ms build, %d curvas/banda máx\n" % [
				name, font.get_glyph_count(),
				font.get_build_time_usec() / 1000.0,
				font.get_max_curves_per_band()
			]
		else:
			text += "%s: inválido\n" % name
	stats_label.text = text


func _process(delta):
	if not orbit_paused:
		orbit_angle += delta * 0.15
		_update_camera()

	if spinning_label:
		spinning_label.rotation_degrees.y += delta * 60.0
		var alpha = 0.65 + 0.35 * sin(OS.get_ticks_msec() / 400.0)
		var m = spinning_label.modulate
		spinning_label.modulate = Color(m.r, m.g, m.b, alpha)

	if _screenshot_path != "":
		_frame_count += 1
		if _frame_count >= 90:
			_take_screenshot()


func _input(event):
	if event is InputEventKey and event.pressed:
		if event.scancode == KEY_SPACE:
			orbit_paused = not orbit_paused
		elif ORBIT_RADII.has(event.scancode):
			orbit_radius = ORBIT_RADII[event.scancode]


func _parse_cmdline_args():
	for arg in OS.get_cmdline_args():
		if arg.begins_with("--screenshot="):
			_screenshot_path = arg.substr(len("--screenshot="))


func _take_screenshot():
	var img = get_viewport().get_texture().get_data()
	img.flip_y()
	var dir = _screenshot_path.get_base_dir()
	if dir != "":
		var d = Directory.new()
		d.make_dir_recursive(dir)
	img.save_png(_screenshot_path)
	get_tree().quit()
