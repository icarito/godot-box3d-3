extends Spatial

# Glow map demo (backport del PR upstream godotengine/godot#93133).
#
# Una sala oscura con barras emisivas de neón y un Environment con glow fuerte.
# Sobre el glow se aplica un `glow_map` (el "lens dirt" de Godot 4): una textura
# que multiplica el glow resultante segun `glow_map_strength`. El mapa se genera
# por codigo (ruido OpenSimplex + unas manchas radiales), asi que el demo no
# depende de ningun asset binario.
#
# Controles:
#   G  prende/apaga el glow
#   M  prende/apaga el glow_map (con el mapa apagado el patron de suciedad no
#      modula nada; es el "antes")
#   [ / ]  baja/sube glow_map_strength
#   R  vuelve a los valores iniciales
#   Esc  sale
#
# Ojo: el glow map es GLES3. Con el rasterizador GLES2 el motor ignora el mapa
# (no-op documentado) y el demo se ve igual con M apagado que encendido.

const INITIAL_STRENGTH := 0.9
const INITIAL_INTENSITY := 0.9

var env: Environment
var camera: Camera
var label: Label

var map_texture: ImageTexture
var glow_enabled := true
var map_enabled := true
var strength := INITIAL_STRENGTH
var intensity := INITIAL_INTENSITY

var panels := []
var time := 0.0


func _ready():
	print("backend=", PhysicsServer.get_class(), "  [G] glow  [M] map  [[/]] strength  [R] reset  [Esc] quit")
	print("glow_map_strength=", strength)

	camera = Camera.new()
	camera.translation = Vector3(0, 2.4, 9.0)
	camera.rotation_degrees = Vector3(-6, 0, 0)
	camera.fov = 55.0
	camera.current = true
	add_child(camera)

	var key := DirectionalLight.new()
	key.rotation_degrees = Vector3(-45, -25, 0)
	key.light_energy = 0.3
	key.shadow_enabled = false
	add_child(key)

	map_texture = _make_lens_dirt(256)

	var world_env := WorldEnvironment.new()
	env = _make_environment()
	world_env.environment = env
	add_child(world_env)

	_build_room()
	_build_panels()

	var canvas := CanvasLayer.new()
	add_child(canvas)
	label = Label.new()
	label.rect_position = Vector2(16, 12)
	label.add_color_override("font_color", Color(0.85, 0.92, 1.0))
	canvas.add_child(label)
	_update_label()


func _make_environment() -> Environment:
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.008, 0.010, 0.018)
	e.glow_enabled = true
	for i in range(5):
		e.set_glow_level(i, true)
	e.glow_intensity = intensity
	e.glow_strength = 1.0
	e.glow_bloom = 0.0
	e.glow_hdr_threshold = 1.0
	e.glow_blend_mode = Environment.GLOW_BLEND_MODE_SCREEN
	e.glow_map = map_texture
	e.glow_map_strength = strength
	return e


func _make_lens_dirt(size: int) -> ImageTexture:
	# Suciedad de lente: base oscura que atenuia el glow en casi toda la
	# pantalla, con manchas radiales que lo dejan pasar. Es lo que hace visible
	# el patron: sin el mapa el glow es uniforme; con el mapa queda moteado.
	var noise := OpenSimplexNoise.new()
	noise.seed = 7
	noise.octaves = 4
	noise.period = 64.0
	noise.persistence = 0.55

	var spots := [
		Vector3(0.22, 0.30, 0.055),
		Vector3(0.72, 0.22, 0.048),
		Vector3(0.58, 0.68, 0.062),
		Vector3(0.35, 0.82, 0.042),
		Vector3(0.86, 0.55, 0.038),
	]

	var img := Image.new()
	img.create(size, size, false, Image.FORMAT_RGB8)
	img.lock()
	for y in range(size):
		for x in range(size):
			var u := float(x) / float(size)
			var v := float(y) / float(size)
			var n := noise.get_noise_2d(float(x), float(y)) * 0.5 + 0.5
			var lum := float(lerp(0.16, 0.80, smoothstep(0.55, 0.85, n)))
			for s in spots:
				var d := Vector2(u - s.x, v - s.y).length() / float(s.z)
				lum += max(0.0, 1.0 - d) * 0.9
			lum = clamp(lum, 0.0, 1.0)
			img.set_pixel(x, y, Color(lum, lum * 0.98, lum * 0.90))
	img.unlock()

	var tex := ImageTexture.new()
	tex.create_from_image(img, 0)
	return tex


func _build_room():
	var floor_mat := SpatialMaterial.new()
	floor_mat.albedo_color = Color(0.045, 0.05, 0.07)
	floor_mat.roughness = 0.85
	var floor_mesh := MeshInstance.new()
	var plane_mesh := PlaneMesh.new()
	plane_mesh.size = Vector2(40, 40)
	floor_mesh.mesh = plane_mesh
	floor_mesh.material_override = floor_mat
	add_child(floor_mesh)

	var wall_mat := SpatialMaterial.new()
	wall_mat.albedo_color = Color(0.03, 0.035, 0.05)
	var wall := MeshInstance.new()
	var quad := QuadMesh.new()
	quad.size = Vector2(40, 18)
	wall.mesh = quad
	wall.translation = Vector3(0, 7, -4.0)
	wall.material_override = wall_mat
	add_child(wall)


func _build_panels():
	var colors := [
		Color(0.15, 0.85, 1.0),
		Color(1.0, 0.25, 0.85),
		Color(1.0, 0.75, 0.20),
		Color(0.30, 1.0, 0.45),
		Color(1.0, 0.30, 0.25),
	]
	for i in range(colors.size()):
		var mat := SpatialMaterial.new()
		mat.albedo_color = Color(0, 0, 0)
		mat.emission_enabled = true
		mat.emission = colors[i]
		mat.emission_energy = 3.5
		_add_panel(mat, i)

	# Un par de esferas flotando, para que el halo se lea en volumen.
	for i in range(3):
		var mat := SpatialMaterial.new()
		mat.albedo_color = Color(0.02, 0.02, 0.02)
		mat.emission_enabled = true
		mat.emission = Color(1.0, 0.9, 0.6)
		mat.emission_energy = 3.0
		var sphere := MeshInstance.new()
		var sm := SphereMesh.new()
		sm.radius = 0.35
		sm.height = 0.7
		sphere.mesh = sm
		sphere.translation = Vector3(-3.0 + i * 3.0, 5.2 + i * 0.35, -3.2)
		sphere.material_override = mat
		add_child(sphere)
		panels.append({"mat": mat, "base": 3.0, "amp": 1.2, "phase": float(i) * 1.7, "speed": 1.3 + 0.2 * i})


func _add_panel(mat: SpatialMaterial, index: int):
	var panel := MeshInstance.new()
	var box := CubeMesh.new()
	box.size = Vector3(0.42, 3.4, 0.18)
	panel.mesh = box
	panel.translation = Vector3(-3.6 + index * 1.8, 3.0, -3.75)
	panel.material_override = mat
	add_child(panel)
	panels.append({"mat": mat, "base": 3.5, "amp": 1.2, "phase": float(index) * 0.9, "speed": 1.1 + 0.15 * index})


func _process(delta):
	time += delta
	for p in panels:
		p.mat.emission_energy = p.base + sin(time * p.speed + p.phase) * p.amp


func _input(event):
	if not (event is InputEventKey) or not event.pressed or event.echo:
		return
	match event.scancode:
		KEY_ESCAPE:
			get_tree().quit()
		KEY_G:
			_toggle_glow()
		KEY_M:
			_toggle_map()
		KEY_BRACKETLEFT:
			_set_strength(strength - 0.1)
		KEY_BRACKETRIGHT:
			_set_strength(strength + 0.1)
		KEY_R:
			_reset()


func _toggle_glow():
	glow_enabled = not glow_enabled
	env.glow_enabled = glow_enabled
	_update_label()
	print("glow_enabled=", glow_enabled)


func _toggle_map():
	map_enabled = not map_enabled
	env.glow_map = map_texture if map_enabled else null
	_update_label()
	print("glow_map=", "on" if map_enabled else "off")


func _set_strength(value: float):
	strength = clamp(value, 0.0, 1.0)
	env.glow_map_strength = strength
	_update_label()
	print("glow_map_strength=", strength)


func _reset():
	glow_enabled = true
	map_enabled = true
	strength = INITIAL_STRENGTH
	intensity = INITIAL_INTENSITY
	env.glow_enabled = true
	env.glow_intensity = intensity
	env.glow_map_strength = strength
	env.glow_map = map_texture
	_update_label()


# Lo usa preview.gd para barrer la fuerza sin tocar el label ni el glow.
func set_preview_strength(value: float):
	strength = clamp(value, 0.0, 1.0)
	if not map_enabled:
		map_enabled = true
		env.glow_map = map_texture
	env.glow_map_strength = strength


func _update_label():
	var driver := "GLES3" if OS.get_current_video_driver() == OS.VIDEO_DRIVER_GLES3 else "GLES2"
	label.text = "Glow map demo - backport PR #93133\n" \
		+ "[G] glow " + ("on" if glow_enabled else "off") \
		+ "   [M] glow_map " + ("on" if map_enabled else "off") \
		+ "   [[ / ]] strength " + ("%.2f" % strength) + "\n" \
		+ "driver " + driver + "   [R] reset   [Esc] quit"