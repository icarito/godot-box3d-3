extends Spatial
# Demo de SlugLabel3D: el .tscn es sólo un Spatial con este script y toda la
# escena se arma en código.
#
# Controles:
#   Espacio  pausa/reanuda la órbita de la cámara
#   1 / 2 / 3  radio de órbita 4 / 9 / 20

const ORBIT_HEIGHT := 2.5
const LOOK_TARGET := Vector3(0, 1.5, 0)
const ORBIT_SPEED := 0.35
const ORBIT_RADII := {KEY_1: 4.0, KEY_2: 9.0, KEY_3: 20.0}

var camera: Camera
var orbit_radius := 9.0
var orbit_angle := 0.0
var orbit_paused := false

var spinning_label: SlugLabel3D
var stats_label: Label
var slug_fonts := []

var _time := 0.0
var _stats_accum := 0.0


func _ready():
	_setup_environment()
	_setup_floor()
	_setup_light()
	_setup_camera()

	var sans_data := DynamicFontData.new()
	sans_data.font_path = "res://fonts/NotoSans-Regular.ttf"
	var serif_data := DynamicFontData.new()
	serif_data.font_path = "res://fonts/NotoSerif-Regular.ttf"

	var sans_font := SlugFont.new()
	sans_font.font_data = sans_data
	var serif_font := SlugFont.new()
	serif_font.font_data = serif_data

	slug_fonts = [["Sans", sans_font], ["Serif", serif_font]]

	if not sans_font.is_valid() or not serif_font.is_valid():
		push_error("SlugFont inválido: faltan las fuentes. Corré demos/slug_text/fetch_fonts.sh")
		_show_missing_fonts_label()
	else:
		_build_labels(sans_font, serif_font, sans_data)

	_setup_hud()
	_maybe_setup_smoke_timer()


func _maybe_setup_smoke_timer():
	# Este build de Godot 3 no tiene --quit-after; con --smoke-test=<segundos>
	# la escena se cierra sola (para el chequeo headless).
	var seconds := -1.0
	for arg in OS.get_cmdline_args():
		if arg.begins_with("--smoke-test="):
			seconds = float(arg.substr(len("--smoke-test=")))
	if seconds > 0.0:
		var timer := Timer.new()
		timer.wait_time = seconds
		timer.one_shot = true
		add_child(timer)
		timer.connect("timeout", self, "_on_smoke_timeout")
		timer.start()


func _on_smoke_timeout():
	get_tree().quit()


func _setup_environment():
	var world_env := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.025, 0.025, 0.04)
	env.ambient_light_color = Color(0.18, 0.18, 0.22)
	env.ambient_light_energy = 1.0
	world_env.environment = env
	add_child(world_env)


func _setup_floor():
	var floor_mesh := MeshInstance.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(30, 30)
	floor_mesh.mesh = plane
	var mat := SpatialMaterial.new()
	mat.albedo_color = Color(0.07, 0.07, 0.09)
	mat.roughness = 0.9
	floor_mesh.material_override = mat
	add_child(floor_mesh)


func _setup_light():
	var light := DirectionalLight.new()
	light.rotation_degrees = Vector3(-50, -30, 0)
	light.shadow_enabled = true
	add_child(light)


func _setup_camera():
	camera = Camera.new()
	add_child(camera)
	_update_camera()


func _update_camera():
	camera.translation = Vector3(sin(orbit_angle) * orbit_radius, ORBIT_HEIGHT, cos(orbit_angle) * orbit_radius)
	camera.look_at(LOOK_TARGET, Vector3.UP)


func _show_missing_fonts_label():
	var canvas := CanvasLayer.new()
	add_child(canvas)
	var label := Label.new()
	label.text = "Faltan las fuentes.\nCorré demos/slug_text/fetch_fonts.sh"
	label.rect_position = Vector2(400, 320)
	canvas.add_child(label)


func _build_labels(sans_font: SlugFont, serif_font: SlugFont, sans_data: DynamicFontData):
	# a) título
	_make_label("Slug en Godot 3.6", serif_font, 1.2, Vector3(0, 3.2, -3), Color(1, 0.95, 0.85))

	# b) test de winding: huecos de letras
	_make_label("O B 8 @ a e & g Q %", sans_font, 0.8, Vector3(0, 1.8, -3), Color(1, 1, 1))

	# c) Latin-1
	_make_label("Ñandú, pingüino, çà, ¿qué? ¡Sí! 1234567890", sans_font, 0.5, Vector3(0, 1.0, -3), Color(1, 1, 1))

	# d) bloque terminal multilínea, alineado a la izquierda, rotado 60° en Y
	var terminal = _make_label("> status: OK\n> temp: 21.5 C\n> slug: GPU", sans_font, 0.25, Vector3(-4, 1.2, 0), Color(0.2, 1, 0.3))
	terminal.align = SlugLabel3D.ALIGN_LEFT
	terminal.rotation_degrees.y = 60

	# e) perspectiva rasante: casi tirado en el piso
	var rasante = _make_label("PERSPECTIVA RASANTE", sans_font, 1.0, Vector3(0, 0.02, 2), Color(1, 1, 1))
	rasante.rotation_degrees.x = -85

	# f) billboard
	var billboard = _make_label("billboard", sans_font, 0.4, Vector3(3.5, 2.2, 1), Color(1, 0.6, 0.1))
	billboard.billboard = true

	# g) animado: gira y pulsa el alpha
	spinning_label = _make_label("girando", sans_font, 1.0, Vector3(3.5, 1, -1), Color(0.6, 0.8, 1, 1))

	# h) comparación con un Label3D normal (DynamicFont)
	var dyn_font := DynamicFont.new()
	dyn_font.font_data = sans_data
	dyn_font.size = 64
	var label3d := Label3D.new()
	label3d.text = "Label3D (DynamicFont)"
	label3d.font = dyn_font
	label3d.pixel_size = 0.005
	label3d.translation = Vector3(0, 0.4, -3)
	add_child(label3d)


func _make_label(text: String, font: SlugFont, size: float, pos: Vector3, color: Color) -> SlugLabel3D:
	var label := SlugLabel3D.new()
	label.text = text
	label.font = font
	label.size = size
	label.translation = pos
	label.modulate = color
	add_child(label)
	return label


func _setup_hud():
	var canvas := CanvasLayer.new()
	add_child(canvas)
	stats_label = Label.new()
	stats_label.rect_position = Vector2(10, 10)
	canvas.add_child(stats_label)
	_refresh_stats()


func _refresh_stats():
	if not stats_label:
		return
	var text := "FPS: %d\n" % Engine.get_frames_per_second()
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
	_time += delta

	if not orbit_paused:
		orbit_angle += delta * ORBIT_SPEED
		_update_camera()

	if spinning_label:
		spinning_label.rotation_degrees.y += delta * 60.0
		var m = spinning_label.modulate
		spinning_label.modulate = Color(m.r, m.g, m.b, 0.65 + 0.35 * sin(_time * 2.5))

	_stats_accum += delta
	if _stats_accum >= 1.0:
		_stats_accum = 0.0
		_refresh_stats()


func _input(event):
	if not (event is InputEventKey) or not event.pressed or event.echo:
		return
	if event.scancode == KEY_SPACE:
		orbit_paused = not orbit_paused
	elif ORBIT_RADII.has(event.scancode):
		orbit_radius = ORBIT_RADII[event.scancode]
