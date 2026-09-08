extends Spatial

# Regression: one ConcavePolygonShape resource used by several bodies.
#
# Box3D only REFERENCES mesh data, it does not copy it, and the local shape
# transform is baked into the vertices. Owning that data on the shared shape
# resource meant the second body destroyed the mesh the first one was still
# pointing at, and both ended up colliding against whichever geometry was
# built last. Reusing one collision mesh across a level is completely normal.

const SETTLE = 200

var ball_a
var ball_b
var frames = 0
var failures = []


func _ready():
	print("backend=", PhysicsServer.get_class())

	var faces = PoolVector3Array()
	faces.push_back(Vector3(-4, 0, -4)); faces.push_back(Vector3(-4, 0, 4)); faces.push_back(Vector3(4, 0, 4))
	faces.push_back(Vector3(-4, 0, -4)); faces.push_back(Vector3(4, 0, 4)); faces.push_back(Vector3(4, 0, -4))
	var shared = ConcavePolygonShape.new()
	shared.set_faces(faces)

	# Same resource, but the second CollisionShape sits 3 m higher, so the two
	# bodies need two differently baked meshes.
	_ground(shared, Vector3(0, 0, 0), Vector3(0, 0, 0))
	_ground(shared, Vector3(20, 0, 0), Vector3(0, 3, 0))

	ball_a = _ball(Vector3(0, 8, 0))
	ball_b = _ball(Vector3(20, 8, 0))


func _ground(p_shape, p_origin, p_local):
	var body = StaticBody.new()
	var col = CollisionShape.new()
	col.shape = p_shape
	col.transform = Transform(Basis(), p_local)
	body.add_child(col)
	add_child(body)
	body.global_transform = Transform(Basis(), p_origin)


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
	var a = ball_a.global_transform.origin.y
	var b = ball_b.global_transform.origin.y
	print("shared trimesh resource:")
	_check(abs(a - 0.5) < 0.15, "ball on the floor at y=0 rests at 0.5 (got %.3f)" % a)
	_check(abs(b - 3.5) < 0.15, "ball on the floor at y=3 rests at 3.5 (got %.3f)" % b)
	if failures.empty():
		print("RESULT m14_shared_trimesh -> PASS")
		get_tree().quit(0)
	else:
		print("RESULT m14_shared_trimesh -> FAIL (%d): %s" % [failures.size(), str(failures)])
		get_tree().quit(1)
