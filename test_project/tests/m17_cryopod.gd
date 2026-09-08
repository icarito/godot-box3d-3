extends Spatial

# Regression from the real cryopod prop. Its colliders live on a KinematicBody
# used as a rotating door, not on a StaticBody, and six ConvexPolygonShapes
# hang off it, four of them flat quads. The player is a KinematicBody too, so
# this is kinematic-versus-kinematic through body_test_motion.
# The pod occupies roughly x -0.35..0.35, so a walker must stop short of it.

const SHAPES = {
	41: PoolVector3Array([Vector3(0.34631, -0.00407, 0.17202), Vector3(0.24063, 1.32967, 0.30214), Vector3(0.24063, -0.00407, 0.30214), Vector3(0.23247, -0.00407, 0.28586), Vector3(0.23247, 1.32967, 0.28586), Vector3(0.35444, 1.32967, 0.1883), Vector3(0.34631, 1.32967, 0.17202), Vector3(0.35444, -0.00407, 0.1883)]),
	42: PoolVector3Array([Vector3(0.23247, 1.32967, 0.29401), Vector3(-0.00337, -0.00407, 0.29401), Vector3(-0.00337, 1.32967, 0.29401), Vector3(0.23247, -0.00407, 0.29401)]),
	43: PoolVector3Array([Vector3(-0.00341, 1.32967, 0.29401), Vector3(-0.23108, -0.00407, 0.29401), Vector3(-0.23108, 1.32967, 0.29401), Vector3(-0.00341, -0.00407, 0.29401)]),
	44: PoolVector3Array([Vector3(-0.33678, -0.00407, 0.18015), Vector3(-0.23109, 1.32967, 0.30214), Vector3(-0.23109, -0.00407, 0.30214), Vector3(-0.35307, 1.32967, 0.18831), Vector3(-0.23109, 1.32967, 0.28586), Vector3(-0.35307, -0.00407, 0.18831), Vector3(-0.33678, 1.32967, 0.18015), Vector3(-0.23109, -0.00407, 0.28586)]),
	45: PoolVector3Array([Vector3(0.35446, -0.00395, -0.29967), Vector3(0.35446, 1.32967, 0.17202), Vector3(0.35446, 1.32967, -0.29967), Vector3(0.35446, -0.00395, 0.17202)]),
	46: PoolVector3Array([Vector3(-0.34493, -0.00407, -0.29962), Vector3(-0.34493, 1.32967, 0.1801), Vector3(-0.34493, 1.32967, -0.29962), Vector3(-0.34493, -0.00407, 0.1801)]),
}

const PLACEMENTS = [
	[41, Transform(Vector3(1.0, 0.0, 0.0), Vector3(0.0, 1.0, 0.0), Vector3(0.0, 0.0, 1.0), Vector3(0.0, 0.0, 0.0))],
	[42, Transform(Vector3(1.0, 0.0, 0.0), Vector3(0.0, 1.0, 0.0), Vector3(0.0, 0.0, 1.0), Vector3(0.0, 0.0, 0.0))],
	[43, Transform(Vector3(1.0, 0.0, 0.0), Vector3(0.0, 1.0, 0.0), Vector3(0.0, 0.0, 1.0), Vector3(0.0, 0.0, 0.0))],
	[44, Transform(Vector3(1.0, 0.0, 0.0), Vector3(0.0, 1.0, 0.0), Vector3(0.0, 0.0, 1.0), Vector3(0.0, 0.0, 0.0))],
	[45, Transform(Vector3(1.0, 0.0, 0.0), Vector3(0.0, 1.0, 0.0), Vector3(0.0, 0.0, 1.0), Vector3(0.0, 0.0, 0.0))],
	[46, Transform(Vector3(1.0, 0.0, 0.0), Vector3(0.0, 1.0, 0.0), Vector3(0.0, 0.0, 1.0), Vector3(0.0, 0.0, 0.0))],
]

var walker
var velocity = Vector3()
var frames = 0
var built = 0
var failures = []


func _ready():
	print("backend=", PhysicsServer.get_class())

	var ground = StaticBody.new()
	var gs = BoxShape.new()
	gs.extents = Vector3(20, 0.5, 20)
	var gc = CollisionShape.new()
	gc.shape = gs
	ground.add_child(gc)
	add_child(ground)
	ground.global_transform = Transform(Basis(), Vector3(0, -0.5, 0))

	# The pod exactly as the scene builds it: KinematicBody, layer 96, mask 255.
	var pod = KinematicBody.new()
	pod.collision_layer = 96
	pod.collision_mask = 255
	add_child(pod)
	pod.global_transform = Transform(Basis(Vector3(-1, 0, 0), Vector3(0, -1, 0), Vector3(0, 0, 1)), Vector3(0, 2.08495, 0.053448))
	for p in PLACEMENTS:
		if not SHAPES.has(p[0]):
			continue
		var shape = ConvexPolygonShape.new()
		shape.points = SHAPES[p[0]]
		var col = CollisionShape.new()
		col.shape = shape
		col.transform = p[1]
		pod.add_child(col)
		built += 1
	print("pod collision shapes: ", built)

	walker = KinematicBody.new()
	walker.collision_layer = 2
	walker.collision_mask = 79
	var ws = BoxShape.new()
	ws.extents = Vector3(0.4, 0.9, 0.4)
	var wc = CollisionShape.new()
	wc.shape = ws
	walker.add_child(wc)
	add_child(walker)
	walker.global_transform = Transform(Basis(), Vector3(-3, 1.2, 0))


func _check(p_ok, p_what):
	if not p_ok:
		failures.append(p_what)
	print(("  ok   " if p_ok else "  FAIL ") + p_what)


func _physics_process(delta):
	frames += 1
	velocity.y -= 24.0 * delta
	if frames > 30:
		velocity.x = 3.0
	velocity = walker.move_and_slide(velocity, Vector3.UP)
	if frames < 160:
		return
	var x = walker.global_transform.origin.x
	print("cryopod:")
	_check(built == 6, "all six pod shapes built (got %d)" % built)
	_check(x < -0.4, "the pod blocks the walker (stopped at x=%.3f)" % x)
	if failures.empty():
		print("RESULT m17_cryopod -> PASS")
		get_tree().quit(0)
	else:
		print("RESULT m17_cryopod -> FAIL (%d): %s" % [failures.size(), str(failures)])
		get_tree().quit(1)
