extends SceneTree

var frame_count = 0
var ready = false

func _initialize():
	var root = get_root()

	var cam = Camera.new()
	root.add_child(cam)
	cam.look_at_from_position(Vector3(0, 3, 0.001), Vector3.ZERO, Vector3(0, 0, -1))
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

	var decal = Decal.new()
	var img = Image.new()
	img.create(64, 64, false, Image.FORMAT_RGBA8)
	img.fill(Color(1, 0, 0, 1))
	var tex = ImageTexture.new()
	tex.create_from_image(img)
	decal.set_texture(Decal.TEXTURE_ALBEDO, tex)
	decal.size = Vector3(2, 2, 2)
	# Rest on the plane so the projection axis fade ~ 1.0 at the surface.
	decal.transform = Transform(Basis(), Vector3(0, 0.01, 0))
	root.add_child(decal)

	ready = true

func _idle(delta):
	if not ready:
		return
	frame_count += 1
	if frame_count < 10:
		return

	var img = get_root().get_texture().get_data()
	img.flip_y()
	img.convert(Image.FORMAT_RGBA8)
	img.lock()

	var w = img.get_width()
	var h = img.get_height()
	var center = img.get_pixel(w / 2, h / 2)
	# Plane spans ~x227..807 at this camera; 68% is on the plane, outside the decal box.
	var edge = img.get_pixel(int(w * 0.68), h / 2)
	img.unlock()

	print("DECAL_TEST center=", center, " edge=", edge)
	img.save_png("/tmp/kilo/decal-test/render.png")

	var ok = center.r > 0.85 and center.g < 0.15 and edge.r > 0.45 and edge.r < 0.6
	if ok:
		print("DECAL_OK")
	else:
		print("DECAL_FAIL")
	quit()
