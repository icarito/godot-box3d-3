extends Spatial

# Regression: a KinematicBody must walk on trimesh level geometry.
#
# The contact phase compares point clouds and meshes have none, so a body resting
# on a trimesh floor used to get its motion clamped to zero by the sweep while
# the contact phase reported nothing: frozen in place, never grounded. This is
# the shape of a real level, where the floor is an imported ConcavePolygonShape.

const GRAVITY = 24.0
const WALK_SPEED = 3.0
const SETTLE_FRAMES = 45
const WALK_FRAMES = 120

var body
var velocity = Vector3()
var state = 0
var frames = 0
var start_x = 0.0
var failures = []


func _ready():
	print("backend=", PhysicsServer.get_class())

	# A 40x40 trimesh slab whose top face sits at y = 0.
	var faces = PoolVector3Array()
	var s = 20.0
	faces.push_back(Vector3(-s, 0, -s)); faces.push_back(Vector3(-s, 0, s)); faces.push_back(Vector3(s, 0, s))
	faces.push_back(Vector3(-s, 0, -s)); faces.push_back(Vector3(s, 0, s)); faces.push_back(Vector3(s, 0, -s))
	var tri = ConcavePolygonShape.new()
	tri.set_faces(faces)
	var ground = StaticBody.new()
	var gcol = CollisionShape.new()
	gcol.shape = tri
	ground.add_child(gcol)
	add_child(ground)

	body = KinematicBody.new()
	var box = BoxShape.new()
	box.extents = Vector3(0.4, 0.9, 0.4)
	var col = CollisionShape.new()
	col.shape = box
	body.add_child(col)
	add_child(body)
	body.global_transform = Transform(Basis(), Vector3(0, 3, 0))


func _check(p_ok, p_what):
	if not p_ok:
		failures.append(p_what)
	print(("  ok   " if p_ok else "  FAIL ") + p_what)


func _physics_process(delta):
	frames += 1
	velocity.y -= GRAVITY * delta
	match state:
		0:
			velocity = body.move_and_slide(velocity, Vector3.UP)
			if frames >= SETTLE_FRAMES:
				print("trimesh floor:")
				var y = body.global_transform.origin.y
				_check(body.is_on_floor(), "is_on_floor() on a trimesh floor")
				_check(abs(y - 0.9) < 0.1, "rests on the trimesh surface (y=%.4f, want 0.9)" % y)
				start_x = body.global_transform.origin.x
				state = 1
				frames = 0
		1:
			velocity.x = WALK_SPEED
			velocity = body.move_and_slide(velocity, Vector3.UP)
			if frames >= WALK_FRAMES:
				var travelled = body.global_transform.origin.x - start_x
				_check(travelled > 3.0, "walks along the trimesh (travelled %.3f m)" % travelled)
				_check(body.is_on_floor(), "still grounded after walking")
				if failures.empty():
					print("RESULT m9_trimesh_walk -> PASS")
					get_tree().quit(0)
				else:
					print("RESULT m9_trimesh_walk -> FAIL (%d): %s" % [failures.size(), str(failures)])
					get_tree().quit(1)
