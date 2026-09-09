extends Spatial

# Regression: a body placed exactly on the floor must still walk.
#
# Level geometry is authored with things snapped to the ground, so a body whose
# base sits at exactly the floor's surface is the normal case, not a corner one.
# At distance zero GJK has no direction to report, and the contact used to come
# back with a near-horizontal normal: is_on_floor() went false and the motion was
# clamped to nothing, so the body stood there buzzing against the ground.

const GRAVITY = 24.0
const WALK_SPEED = 3.0
const FRAMES = 120

var walker
var velocity = Vector3()
var frames = 0
var first_normal = Vector3()
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

	walker = KinematicBody.new()
	var shape = BoxShape.new()
	shape.extents = Vector3(0.4, 0.9, 0.4)
	var col = CollisionShape.new()
	col.shape = shape
	walker.add_child(col)
	add_child(walker)
	# Base exactly at the floor surface: no gap, no overlap.
	walker.global_transform.origin = Vector3(0, 0.9, 0)


func _check(ok, message):
	if not ok:
		failures.append(message)
	print(("  ok   " if ok else "  FAIL ") + message)


func _physics_process(delta):
	frames += 1
	velocity.y -= GRAVITY * delta
	velocity.x = WALK_SPEED
	velocity = walker.move_and_slide(velocity, Vector3.UP)
	if frames == 1 and walker.get_slide_count() > 0:
		first_normal = walker.get_slide_collision(0).normal
	if frames < FRAMES:
		return

	print("flush spawn:")
	_check(first_normal.dot(Vector3.UP) > 0.9, "floor normal points up (got %s)" % first_normal)
	_check(walker.is_on_floor(), "is_on_floor() while resting on it")
	_check(walker.global_transform.origin.x > 3.0, "walks away (x=%.3f)" % walker.global_transform.origin.x)
	print("RESULT m23_flush_spawn -> ", "PASS" if failures.empty() else "FAIL: " + str(failures))
	get_tree().quit(0 if failures.empty() else 1)
