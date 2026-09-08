extends Spatial

# M12: scaled CollisionShape transforms (Collada-style 1/320 import scale).
#   A ConvexPolygonShape whose points are in raw 320x units, with the
#   CollisionShape scaled 0.00313, must collide at the SCALED size and place.

var failures = []
var body

func _ready():
	print("backend=", PhysicsServer.get_class())
	var floor_body = StaticBody.new()
	var fshape = BoxShape.new()
	fshape.extents = Vector3(12, 0.5, 12)
	var fcol = CollisionShape.new()
	fcol.shape = fshape
	floor_body.add_child(fcol)
	add_child(floor_body)
	floor_body.global_transform = Transform(Basis(), Vector3(0, 0, 0))

	# A rigid box built from raw 320x points, scaled down by the shape transform.
	body = RigidBody.new()
	var convex = ConvexPolygonShape.new()
	var e = 0.5 * 320.0 # 160 raw units -> 0.5 m after the 1/320 scale
	convex.points = PoolVector3Array([
		Vector3(-e, -e, -e), Vector3(e, -e, -e), Vector3(e, e, -e), Vector3(-e, e, -e),
		Vector3(-e, -e, e), Vector3(e, -e, e), Vector3(e, e, e), Vector3(-e, e, e),
	])
	var col = CollisionShape.new()
	col.shape = convex
	col.transform = Transform(Basis().scaled(Vector3(1.0 / 320.0, 1.0 / 320.0, 1.0 / 320.0)), Vector3())
	body.add_child(col)
	add_child(body)
	body.global_transform = Transform(Basis(), Vector3(0, 3, 0))

func _check(p_ok, p_what):
	if not p_ok:
		failures.append(p_what)
	print(("  ok   " if p_ok else "  FAIL ") + p_what)

func _physics_process(delta):
	if Engine.get_physics_frames() < 90:
		return

	var space = get_world().direct_space_state
	print("scaled hull:")
	# The scaled box is 1 m wide centered at the body origin, resting on the
	# floor: rays from the side at its mid height must hit it, and it must
	# rest with its bottom at the floor top (y = 0.5).
	_check(abs(body.global_transform.origin.y - 1.0) < 0.06,
		"scaled box rests on the floor (y=%.4f)" % body.global_transform.origin.y)
	var res = space.intersect_ray(Vector3(3, 0.75, 0), Vector3(0.2, 0.75, 0), [], 255)
	_check(not res.empty() and res.collider == body, "ray from +x hits the scaled hull")
	var far = space.intersect_ray(Vector3(3, 0.75, 0), Vector3(-20, 0.75, 0), [body], 255)
	_check(far.empty(), "the ray passes through when the body is excluded")

	_finish()

func _finish():
	if failures.empty():
		print("RESULT m18_scaled_hull -> PASS")
		get_tree().quit(0)
	else:
		print("RESULT m18_scaled_hull -> FAIL (%d): %s" % [failures.size(), str(failures)])
		get_tree().quit(1)
