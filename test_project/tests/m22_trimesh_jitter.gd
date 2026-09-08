extends Spatial

# Regression: a capsule character must sit still against trimesh level geometry.
#
# Convex props go through Box3D's mover planes, but meshes have no point-cloud
# form, so they take the triangle + vertical-probe recovery path instead. That
# path is where a character standing on a mesh floor and leaning on a mesh wall
# buzzes: the symptom Pilot_V2 shows in Odisea, whose level geometry is trimesh.

const GRAVITY = 24.0
const PUSH_SPEED = 3.0
const SETTLE_FRAMES = 60
const MEASURE_FRAMES = 120

var walker
var velocity = Vector3()
var frames = 0
var min_x = INF
var max_x = -INF
var min_y = INF
var max_y = -INF
var failures = []


func _ready():
	print("backend=", PhysicsServer.get_class())
	_trimesh(_quad(Vector3(-20, 0, -20), Vector3(-20, 0, 20), Vector3(20, 0, 20), Vector3(20, 0, -20)))
	# Wall at x = 2, wound the way Godot faces a triangle toward the walker.
	_trimesh(_quad(Vector3(2, 0, -5), Vector3(2, 3, -5), Vector3(2, 3, 5), Vector3(2, 0, 5)))

	walker = KinematicBody.new()
	var shape = CapsuleShape.new()
	shape.radius = 0.5
	shape.height = 1.0
	var collision = CollisionShape.new()
	collision.shape = shape
	# Godot 3 scenes rotate Bullet's Z capsule axis onto world Y.
	collision.transform = Transform(Vector3(-1, 0, 0), Vector3(0, 0, -1), Vector3(0, -1, 0), Vector3())
	walker.add_child(collision)
	add_child(walker)
	walker.global_transform.origin = Vector3(0, 1.2, 0)


func _quad(a, b, c, d):
	var faces = PoolVector3Array()
	faces.push_back(a); faces.push_back(b); faces.push_back(c)
	faces.push_back(a); faces.push_back(c); faces.push_back(d)
	return faces


func _trimesh(faces):
	var shape = ConcavePolygonShape.new()
	shape.set_faces(faces)
	var body = StaticBody.new()
	var collision = CollisionShape.new()
	collision.shape = shape
	body.add_child(collision)
	add_child(body)


func _check(ok, message):
	if not ok:
		failures.append(message)
	print(("  ok   " if ok else "  FAIL ") + message)


func _physics_process(delta):
	frames += 1
	velocity.y -= GRAVITY * delta
	if frames > 20:
		velocity.x = PUSH_SPEED # lean into the wall and keep leaning

	# Exactly how Pilot_V2 moves.
	var snap = Vector3.DOWN * 0.25 if velocity.y <= 0 else Vector3.ZERO
	velocity = walker.move_and_slide_with_snap(velocity, snap, Vector3.UP, true, 4, deg2rad(45), false)

	var origin = walker.global_transform.origin
	if frames >= SETTLE_FRAMES:
		min_x = min(min_x, origin.x)
		max_x = max(max_x, origin.x)
		min_y = min(min_y, origin.y)
		max_y = max(max_y, origin.y)
	if frames < SETTLE_FRAMES + MEASURE_FRAMES:
		return

	print("trimesh jitter:")
	_check(abs(origin.y - 1.0) < 0.1, "rests on the trimesh floor (y=%.4f, want 1.0)" % origin.y)
	_check(max_x - min_x < 0.005, "no horizontal buzz against a trimesh wall (jitter=%.6f)" % (max_x - min_x))
	_check(max_y - min_y < 0.005, "no vertical buzz on a trimesh floor (jitter=%.6f)" % (max_y - min_y))
	_check(origin.x > 1.3, "the wall stops the capsule at its face (x=%.3f)" % origin.x)
	print("RESULT m22_trimesh_jitter -> ", "PASS" if failures.empty() else "FAIL: " + str(failures))
	get_tree().quit(0 if failures.empty() else 1)
