extends Node

# Rango de combate: decals en uso dinámico.
#  - Sombra "blob" que sigue a una caja que patrulla y rebota (se mueve cada frame).
#  - Punto láser que se mueve cada frame (sigue el mouse; en auto, barre solo).
#  - Explosiones: flash emisivo que se desvanece + quemadura persistente,
#    con pool circular por panel. El límite del sistema es 8 decals por
#    objeto: la pared son 3 paneles (cada MeshInstance tiene su propia
#    cuota) y cada uno reserva 5 slots para quemaduras.
# ESC sale. DECAL_DEMO_AUTO=1 corre la secuencia automática y verifica por píxel.

const WALL_Z := -4.0
const SHOT_PATH := "user://shot_combat.png"

var wall_plane := Plane(Vector3(0, 0, 1), WALL_Z)
var rng := RandomNumberGenerator.new()
var auto := false
var frames := 0
var shot_taken := false

var scorches := {}       # panel_idx -> {slots: [Decal], next: int}
var flashes := []        # 3 decals de flash reutilizados
var flash_age := [99.0, 99.0, 99.0]
var flash_idx := 0

var stress := false      # DECAL_DEMO_STRESS=1: same interaction but without quitting, for stress testing
var crate: MeshInstance
var shadow: Decal
var laser: Decal
var aim := Vector3(0, -50, WALL_Z)

func _ready():
	rng.seed = 777
	auto = OS.get_environment("DECAL_DEMO_AUTO") == "1"
	stress = OS.get_environment("DECAL_DEMO_STRESS") == "1"
	_build_scene()

func _build_scene():
	var cam := Camera.new()
	add_child(cam)
	cam.translation = Vector3(0, 1.7, 3.4)
	cam.look_at(Vector3(0, 1.9, WALL_Z), Vector3(0, 1, 0))
	cam.current = true

	var sun := DirectionalLight.new()
	add_child(sun)
	sun.rotation_degrees = Vector3(-55, 25, 0)

	var floor_mi := MeshInstance.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(18, 14)
	floor_mi.mesh = pm
	var fmat := SpatialMaterial.new()
	fmat.albedo_color = Color(0.35, 0.34, 0.33)
	floor_mi.material_override = fmat
	add_child(floor_mi)

	# 3 paneles de pared: cada uno su propio MeshInstance, con su cuota
	# propia de 8 slots de decal (así el pool nunca se agota).
	for i in range(3):
		var wall := MeshInstance.new()
		var qm := QuadMesh.new()
		qm.size = Vector2(3.05, 5)
		wall.mesh = qm
		var wmat := SpatialMaterial.new()
		wmat.albedo_color = Color(0.78, 0.76, 0.72)
		wall.material_override = wmat
		wall.translation = Vector3((i - 1) * 3.1, 2.5, WALL_Z)
		add_child(wall)

		var pool := { "slots": [], "next": 0 }
		for _j in range(5):
			var d := Decal.new()
			d.set_texture(Decal.TEXTURE_ALBEDO, _scorch_tex())
			d.modulate = Color(0.3, 0.27, 0.25, 0.95)
			d.size = Vector3(0.9, 0.5, 0.9)
			d.transform = Transform(_wall_basis(), Vector3(0, -50, WALL_Z))
			add_child(d)
			pool.slots.append(d)
		scorches[i] = pool

	for _i in range(3):
		var f := Decal.new()
		f.set_texture(Decal.TEXTURE_EMISSION, _glow_tex(Color(1.0, 0.55, 0.15)))
		f.emission_energy = 3.0
		f.size = Vector3(1.4, 0.5, 1.4)
		f.transform = Transform(_wall_basis(), Vector3(0, -50, WALL_Z))
		f.visible = false
		add_child(f)
		flashes.append(f)

	crate = MeshInstance.new()
	var bm := CubeMesh.new()
	bm.size = Vector3(0.8, 0.8, 0.8)
	crate.mesh = bm
	var cmat := SpatialMaterial.new()
	cmat.albedo_color = Color(0.45, 0.55, 0.65)
	crate.material_override = cmat
	add_child(crate)

	shadow = Decal.new()
	shadow.set_texture(Decal.TEXTURE_ALBEDO, _shadow_tex())
	shadow.modulate = Color(1, 1, 1, 0.65)
	shadow.size = Vector3(1.3, 1.0, 1.3)
	shadow.transform = Transform(Basis(), Vector3(0, 0.02, -1.4))
	add_child(shadow)

	laser = Decal.new()
	laser.set_texture(Decal.TEXTURE_EMISSION, _glow_tex(Color(1, 0.1, 0.1)))
	laser.emission_energy = 5.0
	laser.size = Vector3(0.18, 0.4, 0.18)
	laser.transform = Transform(_wall_basis(), Vector3(0, -50, WALL_Z))
	add_child(laser)

func _wall_basis() -> Basis:
	return Basis().rotated(Vector3(1, 0, 0), -PI / 2)

func _explode(pos: Vector3):
	var panel := 1
	if pos.x < -1.55:
		panel = 0
	elif pos.x > 1.55:
		panel = 2
	var pool: Dictionary = scorches[panel]
	var d: Decal = pool.slots[pool.next]
	pool.next = (pool.next + 1) % pool.slots.size()
	var jitter := Vector3(rng.randf_range(-0.15, 0.15), rng.randf_range(-0.15, 0.15), 0)
	var s: float = rng.randf_range(0.75, 1.05)
	d.size = Vector3(s, 0.5, s)
	d.transform = Transform(_wall_basis(), pos + jitter)
	d.visible = true

	var fi := flash_idx
	flash_idx = (flash_idx + 1) % flashes.size()
	flashes[fi].transform = Transform(_wall_basis(), pos)
	flashes[fi].modulate = Color(1, 1, 1, 1)
	flashes[fi].visible = true
	flash_age[fi] = 0.0

func _unhandled_input(ev):
	if auto or stress:
		# En modo automático la interacción del usuario invalidaría la
		# secuencia y la verificación: solo ESC sale.
		if ev is InputEventKey and ev.pressed and ev.scancode == KEY_ESCAPE:
			get_tree().quit()
		return
	if ev is InputEventMouseMotion:
		_aim_at_screen(ev.position)
	elif ev is InputEventMouseButton and ev.pressed and ev.button_index == BUTTON_LEFT:
		var cam := get_viewport().get_camera()
		var hit = wall_plane.intersects_ray(cam.project_ray_origin(ev.position), cam.project_ray_normal(ev.position))
		if hit:
			_explode(hit)
	elif ev is InputEventKey and ev.pressed and ev.scancode == KEY_ESCAPE:
		get_tree().quit()

func _aim_at_screen(px: Vector2):
	var cam := get_viewport().get_camera()
	var hit = wall_plane.intersects_ray(cam.project_ray_origin(px), cam.project_ray_normal(px))
	if hit:
		aim = hit
		laser.transform = Transform(_wall_basis(), aim)

func _process(dt):
	var t := frames / 60.0
	frames += 1

	# La caja patrulla en un ocho y rebota; la sombra la sigue cada frame.
	var cp := Vector3(1.7 * sin(t * 0.7), 0.45 + 0.3 * abs(sin(t * 2.2)), -1.4 + 0.9 * sin(t * 1.4))
	crate.translation = cp
	shadow.translation = Vector3(cp.x, 0.02, cp.z)

	if auto:
		aim = Vector3(2.0 * sin(frames * 0.017), 2.3 + 1.1 * sin(frames * 0.023), WALL_Z)
		laser.transform = Transform(_wall_basis(), aim)
	elif stress:
		# Láser barriendo rápido + ~5 explosiones/seg para estresar
		# el pipeline de decals (pool, máscaras y atlas).
		aim = Vector3(2.4 * sin(frames * 0.05), 2.4 + 1.3 * sin(frames * 0.037), WALL_Z)
		laser.transform = Transform(_wall_basis(), aim)
		if frames % 12 == 0:
			_explode(Vector3(rng.randf_range(-2.6, 2.6), rng.randf_range(0.6, 3.6), WALL_Z))
		# Estresa además el recreado de buffers de render (resize),
		# ruta típica de crash al manipular la ventana en juego.
		if frames % 600 == 300:
			OS.window_size = Vector2(800, 520)
		elif frames % 600 == 0:
			OS.window_size = Vector2(1024, 600)
		if frames % 300 == 0:
			print("STRESS frames=", frames)
			var fb := File.new()
			# WRITE (y no READ_WRITE): crea el archivo si no existe.
			if fb.open("/tmp/kilo/stress_beat.txt", File.WRITE) == OK:
				fb.store_line("frames=%d" % frames)
				fb.close()
			if fb:
				fb.seek_end()
				fb.store_line("frames=%d" % frames)
				fb.close()

	for i in range(3):
		if flash_age[i] < 1.0:
			flash_age[i] += dt
			var k: float = clamp(1.0 - flash_age[i] / 0.45, 0.0, 1.0)
			flashes[i].modulate = Color(k, k, k, 1)
			if k <= 0.0:
				flashes[i].visible = false

	if auto:
		if frames >= 30 and (frames - 30) % 40 == 0 and frames <= 230:
			if frames == 30:
				_explode(Vector3(1.0, 1.6, WALL_Z))
			else:
				_explode(Vector3(rng.randf_range(-2.4, 2.4), rng.randf_range(0.7, 3.4), WALL_Z))
		if frames == 330 and not shot_taken:
			shot_taken = true
			_verify_and_quit()

func _verify_and_quit():
	var img := get_viewport().get_texture().get_data()
	img.flip_y()
	img.convert(Image.FORMAT_RGBA8)
	img.save_png(SHOT_PATH)
	var cam := get_viewport().get_camera()
	img.lock()
	var scorch := _luma(_sample(img, cam.unproject_position(Vector3(1.0, 1.6, WALL_Z))))
	var wall := _luma(_sample(img, cam.unproject_position(Vector3(-3.3, 4.4, WALL_Z))))
	var under_crate := _luma(_sample(img, cam.unproject_position(Vector3(crate.translation.x, 0.0, crate.translation.z))))
	var floor_ref := _luma(_sample(img, cam.unproject_position(Vector3(crate.translation.x + 2.2, 0.0, crate.translation.z))))
	var lp := _sample(img, cam.unproject_position(aim))
	img.unlock()
	print("DEMO scorch=", scorch, " wall=", wall, " shadow=", under_crate, " floor=", floor_ref, " laser_rgb=", lp)
	var ok := true
	if scorch > wall - 0.15:
		print("DEMO_FAIL scorch no visible")
		ok = false
	if under_crate > floor_ref - 0.08:
		print("DEMO_FAIL sombra blob no visible")
		ok = false
	if lp.r < lp.g + 0.2 or lp.r < lp.b + 0.2:
		print("DEMO_FAIL laser no rojo")
		ok = false
	print("DEMO_OK" if ok else "DEMO_FAIL")
	get_tree().quit()

func _sample(img: Image, px: Vector2) -> Color:
	var x := clamp(int(px.x), 0, img.get_width() - 1)
	var y := clamp(int(px.y), 0, img.get_height() - 1)
	return img.get_pixel(x, y)

func _luma(c: Color) -> float:
	return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b

func _scorch_tex() -> ImageTexture:
	var img := Image.new()
	img.create(128, 128, false, Image.FORMAT_RGBA8)
	img.lock()
	var phase := 1.7
	for y in range(128):
		for x in range(128):
			var dx := x - 63.5
			var dy := y - 63.5
			var d := sqrt(dx * dx + dy * dy) / 64.0
			var ang := atan2(dy, dx)
			var r := 0.58 + 0.12 * sin(3.0 * ang + phase) + 0.05 * sin(5.0 * ang + phase * 2.1)
			if d < r:
				var a: float = clamp((r - d) / 0.16, 0.0, 1.0)
				var g: float = 0.10 + 0.10 * clamp((r - d) / r, 0.0, 1.0)
				img.set_pixel(x, y, Color(g * 0.55, g * 0.5, g * 0.45, a))
	img.unlock()
	var t := ImageTexture.new()
	t.create_from_image(img)
	return t

func _shadow_tex() -> ImageTexture:
	var img := Image.new()
	img.create(128, 128, false, Image.FORMAT_RGBA8)
	img.lock()
	for y in range(128):
		for x in range(128):
			var dx := x - 63.5
			var dy := y - 63.5
			var d := sqrt(dx * dx + dy * dy) / 64.0
			var a := pow(clamp(1.0 - d, 0.0, 1.0), 1.6)
			img.set_pixel(x, y, Color(0, 0, 0, a))
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
			# El shader samplea solo RGB para emisión (igual que el shader
			# de 4.x, sin multiplicar por alfa): pre-multiplicar deja el
			# borde del quad en negro, y aditivo sobre negro es invisible,
			# así el glow se ve redondo y no cuadrado.
			img.set_pixel(x, y, Color(col.r * a, col.g * a, col.b * a, a))
	img.unlock()
	var t := ImageTexture.new()
	t.create_from_image(img)
	return t
