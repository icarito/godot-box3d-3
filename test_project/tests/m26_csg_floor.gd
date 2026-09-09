extends Spatial

# Regression: a RigidBody must rest on a CSG shape's collision.
#
# Test levels are built out of CSGBoxes with use_collision on. A character
# walking on one is resolved by the sweep, but a falling crate is resolved by
# the solver, and only the crate fell through.

const FRAMES = 150
var box
var frames = 0
var failures = []


func _ready():
	print("backend=", PhysicsServer.get_class())
	# Same shape as the test level's floor: thin, and far from the origin.
	# Exactly the test level's arrangement: the CSG sits under a parent that
	# carries its own translation.
	var level = Spatial.new()
	add_child(level)
	level.transform.origin = Vector3(-20.8183, 0, 25.2046)

	var csg = CSGBox.new()
	csg.width = 20
	csg.height = 0.2
	csg.depth = 58
	csg.use_collision = true
	level.add_child(csg)
	csg.transform.origin = Vector3(21, -0.1, 0)

	box = RigidBody.new()
	var shape = BoxShape.new()
	shape.extents = Vector3(0.5, 0.5, 0.5)
	var col = CollisionShape.new()
	col.shape = shape
	box.add_child(col)
	add_child(box)
	box.global_transform.origin = Vector3(0, 1, 3)


func _check(ok, message):
	if not ok:
		failures.append(message)
	print(("  ok   " if ok else "  FAIL ") + message)


func _physics_process(_delta):
	frames += 1
	if frames < FRAMES:
		return
	var y = box.global_transform.origin.y
	print("csg floor:")
	_check(abs(y - 0.5) < 0.15, "CSG floor holds the box up (y=%.3f, want 0.5)" % y)
	print("RESULT m26_csg_floor -> ", "PASS" if failures.empty() else "FAIL: " + str(failures))
	get_tree().quit(0 if failures.empty() else 1)
