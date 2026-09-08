extends Spatial

var failures = []


func _ready():
	var parent = Spatial.new()
	parent.scale = Vector3.ONE * 1.5
	add_child(parent)

	var body = StaticBody.new()
	parent.add_child(body)
	var collision = CollisionShape.new()
	var shape = BoxShape.new()
	shape.extents = Vector3(0.2, 0.5, 0.2)
	collision.shape = shape
	collision.translation.z = 8.0
	body.add_child(collision)

	yield(get_tree(), "physics_frame")
	var space = get_world().direct_space_state
	_check(space.intersect_ray(Vector3(0, 0, 7), Vector3(0, 0, 9)).empty(),
			"scaled body's shape is absent at its unscaled position")
	_check(not space.intersect_ray(Vector3(0, 0, 11), Vector3(0, 0, 13)).empty(),
			"scaled body's shape is present at its visual position")

	print("RESULT m20_body_scale -> ", "PASS" if failures.empty() else "FAIL: " + str(failures))
	get_tree().quit(0 if failures.empty() else 1)


func _check(ok, what):
	if not ok:
		failures.append(what)
	print(("  ok   " if ok else "  FAIL ") + what)
