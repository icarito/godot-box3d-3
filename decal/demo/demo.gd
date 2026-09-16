extends Node

# Rango de tiro: clic izquierdo proyecta un decal en la pared
# (hueco de bala o salpicadura, a veces emisivo). ESC sale.
# DECAL_DEMO_AUTO=1 corre la secuencia automática y verifica por píxel.

const WALL_Z := -4.0
const SHOT_PATH := "user://shot_range.png"

var wall_plane := Plane(Vector3(0, 0, 1), WALL_Z)
var rng := RandomNumberGenerator.new()
var auto := false
var frames := 0
var shot_taken := false

func _ready():
	rng.seed = 20260916
	auto = OS.get_environment("DECAL_DEMO_AUTO") == "1"
	_build_scene()

func _build_scene():
	var cam := Camera.new()
	add_child(cam)
	cam.translation = Vector3(0, 1.6, 3.2)
	cam.look_at(Vector3(0, 1.9, WALL_Z), Vector3(0, 1, 0))
	cam.current = true

	var sun := DirectionalLight.new()
	add_child(sun)
	sun.rotation_degrees = Vector3(-55, 25, 0)

	var wall := MeshInstance.new()
	var qm := QuadMesh.new()
	qm.size = Vector2(9, 5)
	wall.mesh = qm
	var wmat := SpatialMaterial.new()
	wmat.albedo_color = Color(0.78, 0.76, 0.72)
	wall.material_override = wmat
	wall.translation = Vector3(0, 2.5, WALL_Z)
	add_child(wall)

	var floor_mi := MeshInstance.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(16, 16)
	floor_mi.mesh = pm
	var fmat := SpatialMaterial.new()
	fmat.albedo_color = Color(0.35, 0.34, 0.33)
	floor_mi.material_override = fmat
	add_child(floor_mi)

	# Decoración fija: el efecto se ve desde el primer frame.
	_spawn_splat(Vector3(-1.7, 1.35, WALL_Z), Color(0.92, 0.18, 0.45), 0.6, false)
	_spawn_splat(Vector3(1.8, 2.7, WALL_Z), Color(0.25, 0.55, 1.0), 0.45, false)
	_spawn_splat(Vector3(0.8, 0.0, -1.4), Color(0.95, 0.75, 0.2), 0.7, false, true)
	_spawn_bullet(Vector3(0.7, 1.8, WALL_Z))

func _wall_basis() -> Basis:
	return Basis().rotated(Vector3(1, 0, 0), -PI / 2)

func _spawn_bullet(pos: Vector3):
	var d := Decal.new()
	d.set_texture(Decal.TEXTURE_ALBEDO, _bullet_tex())
	d.size = Vector3(0.42, 0.5, 0.42)
	d.transform = Transform(_wall_basis(), pos)
	add_child(d)

func _spawn_splat(pos: Vector3, col: Color, s: float, emissive: bool, floor_mode := false):
	var d := Decal.new()
	d.set_texture(Decal.TEXTURE_ALBEDO, _splat_tex(col))
	if emissive:
		d.set_texture(Decal.TEXTURE_EMISSION, _glow_tex(col))
		d.emission_energy = 2.5
	if floor_mode:
		d.size = Vector3(s, 2.0, s)
		d.transform = Transform(Basis(), pos + Vector3(0, 0.01, 0))
	else:
		d.size = Vector3(s, 0.5, s)
		d.transform = Transform(_wall_basis(), pos)
	add_child(d)

func _splat_tex(col: Color) -> ImageTexture:
	var img := Image.new()
	img.create(128, 128, false, Image.FORMAT_RGBA8)
	img.lock()
	var phase := rng.randf() * TAU
	for y in range(128):
		for x in range(128):
			var dx := x - 63.5
			var dy := y - 63.5
			var d := sqrt(dx * dx + dy * dy) / 64.0
			var ang := atan2(dy, dx)
			var r := 0.62 + 0.10 * sin(3.0 * ang + phase) + 0.04 * sin(5.0 * ang + phase * 2.3)
			if d < r:
				var a: float = clamp((r - d) / 0.18, 0.0, 1.0)
				var shade: float = 1.0 - 0.3 * (d / max(r, 0.001))
				img.set_pixel(x, y, Color(col.r * shade, col.g * shade, col.b * shade, a))
	img.unlock()
	var t := ImageTexture.new()
	t.create_from_image(img)
	return t

func _bullet_tex() -> ImageTexture:
	var img := Image.new()
	img.create(128, 128, false, Image.FORMAT_RGBA8)
	img.lock()
	for y in range(128):
		for x in range(128):
			var dx := x - 63.5
			var dy := y - 63.5
			var d := sqrt(dx * dx + dy * dy) / 64.0
			var col := Color(0, 0, 0, 0)
			if d < 0.26:
				col = Color(0.06, 0.05, 0.045, 1.0)
			elif d < 0.55:
				var t := (d - 0.26) / 0.29
				col = Color(0.20, 0.18, 0.17, 1.0 - 0.75 * t)
			elif d < 0.68:
				col = Color(0.55, 0.52, 0.48, 0.35 * (1.0 - (d - 0.55) / 0.13))
			img.set_pixel(x, y, col)
	img.unlock()
	var t := ImageTexture.new()
	t.create_from_image(img)
	return t

func _glow_tex(col: Color) -> ImageTexture:
	var img := Image.new()
	img.create(128, 128, false, Image.FORMAT_RGBA8)
	img.lock()
	for y in range(128):
		for x in range(128):
			var dx := x - 63.5
			var dy := y - 63.5
			var d := sqrt(dx * dx + dy * dy) / 64.0
			var a := pow(clamp(1.0 - d, 0.0, 1.0), 2.0)
			img.set_pixel(x, y, Color(col.r, col.g, col.b, a))
	img.unlock()
	var t := ImageTexture.new()
	t.create_from_image(img)
	return t

func _unhandled_input(ev):
	if ev is InputEventMouseButton and ev.pressed and ev.button_index == BUTTON_LEFT:
		var cam := get_viewport().get_camera()
		var hit = wall_plane.intersects_ray(cam.project_ray_origin(ev.position), cam.project_ray_normal(ev.position))
		if hit:
			if rng.randf() < 0.45:
				_spawn_bullet(hit)
			else:
				_spawn_splat(hit, Color.from_hsv(rng.randf(), 0.8, 0.95), rng.randf_range(0.35, 0.65), rng.randf() < 0.25)
	elif ev is InputEventKey and ev.pressed and ev.scancode == KEY_ESCAPE:
		get_tree().quit()

func _process(_dt):
	if not auto:
		return
	frames += 1
	if frames >= 20 and frames <= 95 and (frames % 15) == 5:
		var p := Vector3(rng.randf_range(-2.4, 2.4), rng.randf_range(0.6, 3.4), WALL_Z)
		if (frames / 15) % 2 == 0:
			_spawn_bullet(p)
		else:
			_spawn_splat(p, Color.from_hsv(rng.randf(), 0.8, 0.95), rng.randf_range(0.4, 0.7), rng.randf() < 0.3)
	if frames == 120 and not shot_taken:
		shot_taken = true
		_verify_and_quit()

func _verify_and_quit():
	var img := get_viewport().get_texture().get_data()
	img.flip_y()
	img.convert(Image.FORMAT_RGBA8)
	img.save_png(SHOT_PATH)
	var cam := get_viewport().get_camera()
	var center := cam.unproject_position(Vector3(0.7, 1.8, WALL_Z))
	var ctrl := cam.unproject_position(Vector3(-3.4, 4.2, WALL_Z))
	img.lock()
	var c1 := _sample(img, center)
	var c2 := _sample(img, ctrl)
	img.unlock()
	var lum_c: float = 0.299 * c1.r + 0.587 * c1.g + 0.114 * c1.b
	var lum_w: float = 0.299 * c2.r + 0.587 * c2.g + 0.114 * c2.b
	print("DEMO hole_luma=", lum_c, " wall_luma=", lum_w)
	if lum_c < lum_w - 0.25:
		print("DEMO_OK")
	else:
		print("DEMO_FAIL")
	get_tree().quit()

func _sample(img: Image, px: Vector2) -> Color:
	var x := clamp(int(px.x), 0, img.get_width() - 1)
	var y := clamp(int(px.y), 0, img.get_height() - 1)
	return img.get_pixel(x, y)
