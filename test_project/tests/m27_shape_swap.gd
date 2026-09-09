extends Spatial

# Regression: replacing a CollisionShape's resource at runtime must keep the
# body colliding.
#
# Sizing a prop from script is a common pattern -- duplicate the shape so the
# resource is not shared, then set its extents. A body whose shape was swapped
# this way stopped colliding entirely and fell through the floor.

const FRAMES = 150
var swapped
var edited
var frames = 0
var failures = []


func _ready():
	print("backend=", PhysicsServer.get_class())
	var floor_body = StaticBody.new()
	var fshape = BoxShape.new()
	fshape.extents = Vector3(20, 0.5, 20)
	var fcol = CollisionShape.new()
	fcol.shape = fshape
	floor_body.add_child(fcol)
	add_child(floor_body)
	floor_body.global_transform.origin = Vector3(0, -0.5, 0)

	swapped = _crate(Vector3(0, 2, 0), true, false)
	edited = _crate(Vector3(5, 2, 0), true, true)


func _crate(pos, swap, edit):
	var body = RigidBody.new()
	var col = CollisionShape.new()
	col.shape = BoxShape.new()
	col.shape.extents = Vector3(0.5, 0.5, 0.5)
	body.add_child(col)
	add_child(body)
	body.global_transform.origin = pos
	# After the body is in the world, exactly like the game's crate does.
	if swap:
		col.shape = col.shape.duplicate()
	if edit:
		col.shape.extents = Vector3(1, 1, 1)
	return body


func _check(ok, message):
	if not ok:
		failures.append(message)
	print(("  ok   " if ok else "  FAIL ") + message)


func _physics_process(_delta):
	frames += 1
	if frames < FRAMES:
		return
	print("runtime shape swap:")
	_check(abs(swapped.global_transform.origin.y - 0.5) < 0.15,
		"duplicated shape still collides (y=%.3f, want 0.5)" % swapped.global_transform.origin.y)
	_check(abs(edited.global_transform.origin.y - 1.0) < 0.15,
		"duplicated then resized shape collides (y=%.3f, want 1.0)" % edited.global_transform.origin.y)
	print("RESULT m27_shape_swap -> ", "PASS" if failures.empty() else "FAIL: " + str(failures))
	get_tree().quit(0 if failures.empty() else 1)
