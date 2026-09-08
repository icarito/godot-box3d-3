extends Spatial

# Visual demo for the Box3D backend. Builds the scene in code so there is no
# .tscn to keep in sync. R respawns the boxes, Escape quits.

var rng = RandomNumberGenerator.new()


func _ready():
	print("backend=", PhysicsServer.get_class(), "  [R] respawn  [Esc] quit")

	var light = DirectionalLight.new()
	light.rotation_degrees = Vector3(-55, -35, 0)
	light.light_energy = 1.2
	add_child(light)

	var camera = Camera.new()
	camera.translation = Vector3(9, 6, 11)
	camera.look_at_from_position(Vector3(9, 6, 11), Vector3(0, 1.5, 0), Vector3.UP)
	add_child(camera)

	_add_floor()
	_spawn_boxes()


func _spawn_boxes():
	# A loose stack plus a couple of tumblers, so contacts, friction, rotation
	# and sleeping are all visible in one shot.
	for child in get_children():
		if child is RigidBody:
			child.queue_free()

	rng.seed = OS.get_ticks_msec()
	for i in range(7):
		var t = Transform.IDENTITY
		t.origin = Vector3(rng.randf_range(-0.35, 0.35), 1.5 + i * 1.4, rng.randf_range(-0.35, 0.35))
		t.basis = Basis(Vector3(0, 1, 0), rng.randf_range(-0.4, 0.4))
		_add_box(t, Color(0.25 + 0.1 * i, 0.55, 0.85 - 0.07 * i))

	_add_box(Transform(Basis(Vector3(0, 0, 1), 0.6), Vector3(-3.2, 7.0, 0.4)), Color(0.9, 0.45, 0.2))
	_add_box(Transform(Basis(Vector3(1, 0, 0), 0.9), Vector3(3.0, 9.0, -0.6)), Color(0.9, 0.75, 0.25))


func _add_floor():
	var body = StaticBody.new()
	add_child(body)

	var shape = BoxShape.new()
	shape.extents = Vector3(9, 0.5, 9)
	var col = CollisionShape.new()
	col.shape = shape
	body.add_child(col)

	var mesh = MeshInstance.new()
	var cube = CubeMesh.new()
	cube.size = shape.extents * 2.0
	mesh.mesh = cube
	mesh.material_override = _material(Color(0.35, 0.37, 0.4))
	body.add_child(mesh)


func _add_box(p_transform, p_color):
	var body = RigidBody.new()
	add_child(body)
	body.global_transform = p_transform

	var shape = BoxShape.new()
	shape.extents = Vector3(0.5, 0.5, 0.5)
	var col = CollisionShape.new()
	col.shape = shape
	body.add_child(col)

	var mesh = MeshInstance.new()
	var cube = CubeMesh.new()
	# CubeMesh defaults to 2x2x2, BoxShape.extents is a half-size: match them or
	# the render looks like the physics is wrong when it is not.
	cube.size = shape.extents * 2.0
	mesh.mesh = cube
	mesh.material_override = _material(p_color)
	body.add_child(mesh)


func _material(p_color):
	var mat = SpatialMaterial.new()
	mat.albedo_color = p_color
	mat.roughness = 0.7
	return mat


func _unhandled_input(event):
	if not event is InputEventKey or not event.pressed:
		return
	if event.scancode == KEY_R:
		_spawn_boxes()
	elif event.scancode == KEY_ESCAPE:
		get_tree().quit()
