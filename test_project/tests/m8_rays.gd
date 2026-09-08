extends Spatial

# M8 acceptance: ray shapes through the stock Godot API.
#   A kinematic character whose only shape is a downward RayShape (the classic
#   ray-feet controller) must stand on the floor through move_and_slide:
#   gravity sinks it a few millimetres per frame, the ray separation pushes it
#   back, and the separation hit sets is_on_floor().
#
# Ray extends along the shape's local +Z; the CollisionShape is rotated so it
# points down. Floor top is y = 0.5, so the body rests near y = 1.5.

const GRAVITY = 24.0
const SETTLE_FRAMES = 90

var body
var velocity = Vector3()
var state = 0
var frames = 0
var failures = []
var mc_result

func _ready():
	print("backend=", PhysicsServer.get_class())
	var floor_body = StaticBody.new()
	var shape = BoxShape.new()
	shape.extents = Vector3(10, 0.5, 10)
	var col = CollisionShape.new()
	col.shape = shape
	floor_body.add_child(col)
	add_child(floor_body)
	floor_body.global_transform = Transform(Basis(), Vector3(0, 0, 0))

	body = KinematicBody.new()
	var ray = RayShape.new()
	ray.length = 1.0
	var col2 = CollisionShape.new()
	col2.shape = ray
	# Rotate the shape's +Z onto world -Y.
	col2.transform = Transform(Basis(Vector3(1, 0, 0), PI * 0.5), Vector3())
	body.add_child(col2)
	add_child(body)
	body.global_transform = Transform(Basis(), Vector3(0, 1.6, 0))

func _check(p_ok, p_what):
	if not p_ok:
		failures.append(p_what)
	print(("  ok   " if p_ok else "  FAIL ") + p_what)

func _physics_process(delta):
	frames += 1
	match state:
		0:
			# A direct move_and_collide with rays included must sweep the ray.
			var col = body.move_and_collide(Vector3(0, -2, 0), true, false)
			_check(col != null, "move_and_collide sweeps the ray onto the floor")
			if col != null:
				_check(col.normal.dot(Vector3.UP) > 0.9, "ray sweep normal points up (got %s)" % col.normal)
				_check(col.collider == get_child(0), "ray sweep names the floor")
			state = 1
			frames = 0
		1:
			velocity.y -= GRAVITY * delta
			velocity = body.move_and_slide(velocity, Vector3.UP)
			if frames >= SETTLE_FRAMES:
				var y = body.global_transform.origin.y
				print("ray feet:")
				_check(body.is_on_floor(), "ray-feet character is_on_floor() after settling")
				_check(abs(y - 1.5) < 0.06, "body rests with the ray tip on the floor (y=%.4f)" % y)
				_check(body.get_floor_normal().dot(Vector3.UP) > 0.9,
					"floor normal points up (got %s)" % body.get_floor_normal())
				_check(body.get_floor_velocity() == Vector3(), "floor velocity is zero")
				_finish()

func _finish():
	if failures.empty():
		print("RESULT m8_rays -> PASS")
		get_tree().quit(0)
	else:
		print("RESULT m8_rays -> FAIL (%d): %s" % [failures.size(), str(failures)])
		get_tree().quit(1)
