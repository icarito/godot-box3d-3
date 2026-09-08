extends Spatial

# M3 acceptance: KinematicBody must work through the stock Godot API.
#   1. move_and_collide() stops on the floor and reports an upward normal.
#   2. move_and_slide() settles the body and reports is_on_floor().
#   3. walking into a wall stops the body at the wall face.
#
# Floor top is y = 0.5 and the body is a 1m cube, so it rests at y = 1.0.
# Wall face is x = 4.5, so a walking body stops at x = 4.0.

const TOLERANCE = 0.06
const SETTLE_FRAMES = 60
const WALK_FRAMES = 150
const WALK_SPEED = 4.0
const GRAVITY = 24.0

var body
var velocity = Vector3()
var state = 0
var frames = 0
var failures = []


func _ready():
	print("backend=", PhysicsServer.get_class())
	_add_static(Vector3(10, 0.5, 10), Vector3(0, 0, 0))
	_add_static(Vector3(0.5, 2, 10), Vector3(5, 2, 0))

	body = KinematicBody.new()
	var shape = BoxShape.new()
	shape.extents = Vector3(0.5, 0.5, 0.5)
	var col = CollisionShape.new()
	col.shape = shape
	body.add_child(col)
	add_child(body)
	body.global_transform = Transform(Basis(), Vector3(0, 3, 0))


func _add_static(p_extents, p_origin):
	var static_body = StaticBody.new()
	var shape = BoxShape.new()
	shape.extents = p_extents
	var col = CollisionShape.new()
	col.shape = shape
	static_body.add_child(col)
	add_child(static_body)
	static_body.global_transform = Transform(Basis(), p_origin)


func _check(p_ok, p_what):
	if not p_ok:
		failures.append(p_what)
	print(("  ok   " if p_ok else "  FAIL ") + p_what)


func _physics_process(delta):
	frames += 1
	match state:
		0:
			_test_move_and_collide()
			state = 1
			frames = 0
		1:
			velocity.y -= GRAVITY * delta
			velocity = body.move_and_slide(velocity, Vector3.UP)
			if frames >= SETTLE_FRAMES:
				print("move_and_slide settle:")
				_check(body.is_on_floor(), "is_on_floor() after settling")
				_check(abs(body.global_transform.origin.y - 1.0) < TOLERANCE,
					"rests at y=1.0 (got %.4f)" % body.global_transform.origin.y)
				state = 2
				frames = 0
		2:
			velocity.y -= GRAVITY * delta
			velocity.x = WALK_SPEED
			velocity = body.move_and_slide(velocity, Vector3.UP)
			if frames >= WALK_FRAMES:
				var x = body.global_transform.origin.x
				print("walk into wall:")
				_check(abs(x - 4.0) < TOLERANCE, "stops at wall x=4.0 (got %.4f)" % x)
				_check(body.is_on_floor(), "still on floor while walking")
				_finish()


func _test_move_and_collide():
	print("move_and_collide:")
	var col = body.move_and_collide(Vector3(0, -5, 0))
	_check(col != null, "collides with the floor")
	if col != null:
		_check(col.normal.dot(Vector3.UP) > 0.9, "normal points up (got %s)" % col.normal)
		_check(col.collider == get_child(0), "names the floor as the collider")
	_check(abs(body.global_transform.origin.y - 1.0) < TOLERANCE,
		"lands at y=1.0 (got %.4f)" % body.global_transform.origin.y)


func _finish():
	if failures.empty():
		print("RESULT m3_kinematic -> PASS")
		get_tree().quit(0)
	else:
		print("RESULT m3_kinematic -> FAIL (%d): %s" % [failures.size(), str(failures)])
		get_tree().quit(1)
