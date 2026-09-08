extends Spatial

# Regression: a CollisionShape's local basis carries scale, and level geometry
# exported from modelling tools is full of mirrored ones (negative determinant).
# Keeping only the rotation puts the collider at the wrong size, and for a
# mirror on the wrong side entirely, which reads in game as "no collision".
#
# The shape is deliberately asymmetric: its points sit at local x 1..3, so a
# mirror on X moves it to -3..-1. A symmetric box would hide the bug.

const SETTLE = 150

var scaled_ball
var mirror_ball
var control_ball
var failures = []
var frames = 0


func _slab_points(p_half_height):
	var pts = PoolVector3Array()
	for x in [1.0, 3.0]:
		for y in [-p_half_height, p_half_height]:
			for z in [-1.5, 1.5]:
				pts.push_back(Vector3(x, y, z))
	return pts


func _ready():
	print("backend=", PhysicsServer.get_class())

	# 1. Scale 3x on Y. Half height 0.5 becomes 1.5, and the shape origin at
	# y=1.0 is a translation, not scaled, so the top lands at y = 2.5.
	_platform(Vector3(0, 0, 0), Basis().scaled(Vector3(1, 3, 1)), Vector3(0, 1.0, 0))
	scaled_ball = _ball(Vector3(2, 8, 0))

	# 2. Mirror on X: the slab moves from x 1..3 to x -3..-1.
	_platform(Vector3(20, 0, 0), Basis().scaled(Vector3(-1, 1, 1)), Vector3(0, 1.0, 0))
	mirror_ball = _ball(Vector3(18, 6, 0))   # over the mirrored side
	control_ball = _ball(Vector3(22, 6, 0))  # over where it would sit unmirrored


func _platform(p_body_origin, p_basis, p_local_origin):
	var body = StaticBody.new()
	var shape = ConvexPolygonShape.new()
	shape.points = _slab_points(0.5)
	var col = CollisionShape.new()
	col.shape = shape
	col.transform = Transform(p_basis, p_local_origin)
	body.add_child(col)
	add_child(body)
	body.global_transform = Transform(Basis(), p_body_origin)


func _ball(p_origin):
	var body = RigidBody.new()
	var col = CollisionShape.new()
	var s = SphereShape.new()
	s.radius = 0.5
	col.shape = s
	body.add_child(col)
	add_child(body)
	body.global_transform = Transform(Basis(), p_origin)
	return body


func _check(p_ok, p_what):
	if not p_ok:
		failures.append(p_what)
	print(("  ok   " if p_ok else "  FAIL ") + p_what)


func _physics_process(_delta):
	frames += 1
	if frames < SETTLE:
		return
	print("shape basis:")
	var a = scaled_ball.global_transform.origin.y
	var m = mirror_ball.global_transform.origin.y
	var c = control_ball.global_transform.origin.y
	_check(abs(a - 3.0) < 0.15, "3x scaled slab holds the ball at 3.0 (got %.3f)" % a)
	_check(abs(m - 2.0) < 0.15, "mirrored slab holds the ball on the -x side at 2.0 (got %.3f)" % m)
	_check(c < 0.0, "nothing left on the unmirrored side (control ball fell to %.2f)" % c)
	if failures.empty():
		print("RESULT m15_shape_scale -> PASS")
		get_tree().quit(0)
	else:
		print("RESULT m15_shape_scale -> FAIL (%d): %s" % [failures.size(), str(failures)])
		get_tree().quit(1)
