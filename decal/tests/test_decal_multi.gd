extends SceneTree

var frame_count = 0
var ready = false

func _initialize():
	var root = get_root()

	var cam = Camera.new()
	root.add_child(cam)
	cam.look_at_from_position(Vector3(0, 3.5, 0.001), Vector3.ZERO, Vector3(0, 0, -1))
	cam.current = true

	var sun = DirectionalLight.new()
	sun.rotation_degrees = Vector3(-90, 0, 0)
	root.add_child(sun)

	var mi = MeshInstance.new()
	var pm = PlaneMesh.new()
	pm.size = Vector2(4, 4)
	mi.mesh = pm
	var mat = SpatialMaterial.new()
	mat.albedo_color = Color(0.5, 0.5, 0.5)
	mi.material_override = mat
	root.add_child(mi)

	# Decal 1: red albedo, center.
	var d1 = Decal.new()
	var img1 = Image.new()
	img1.create(64, 64, false, Image.FORMAT_RGBA8)
	img1.fill(Color(1, 0, 0, 1))
	var t1 = ImageTexture.new()
	t1.create_from_image(img1)
	d1.set_texture(Decal.TEXTURE_ALBEDO, t1)
	d1.size = Vector3(1.2, 2, 1.2)
	d1.transform = Transform(Basis(), Vector3(-0.7, 0.01, 0))
	root.add_child(d1)

	# Decal 2: green albedo, right side.
	var d2 = Decal.new()
	var img2 = Image.new()
	img2.create(64, 64, false, Image.FORMAT_RGBA8)
	img2.fill(Color(0, 1, 0, 1))
	var t2 = ImageTexture.new()
	var t2b = ImageTexture.new()
	t2b.create_from_image(img2)
	d2.set_texture(Decal.TEXTURE_ALBEDO, t2b)
	d2.size = Vector3(1.2, 2, 1.2)
	d2.transform = Transform(Basis(), Vector3(0.9, 0.01, 0.3))
	root.add_child(d2)

	# Decal 3: emission-only (magenta light, alpha inside a soft circle).
	var d3 = Decal.new()
	var img3 = Image.new()
	img3.create(64, 64, false, Image.FORMAT_RGBA8)
	img3.lock()
	for y in range(64):
		for x in range(64):
			var dx = x - 32
			var dy = y - 32
			var d = sqrt(float(dx * dx + dy * dy)) / 32.0
			var a = clamp(1.0 - d, 0.0, 1.0)
			img3.set_pixel(x, y, Color(1, 1, 0, a))
	img3.unlock()
	var t3 = ImageTexture.new()
	t3.create_from_image(img3)
	d3.set_texture(Decal.TEXTURE_EMISSION, t3)
	d3.size = Vector3(1.0, 2, 1.0)
	d3.transform = Transform(Basis(), Vector3(0, 0.02, -0.9))
	root.add_child(d3)

func _idle(delta):
	frame_count += 1
	if frame_count < 10:
		return

	var img = get_root().get_texture().get_data()
	img.flip_y()
	img.convert(Image.FORMAT_RGBA8)
	img.save_png("/tmp/kilo/decal-test/render_multi.png")
	print("DECAL_MULTI_DONE")
	quit()
