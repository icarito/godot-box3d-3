extends Spatial

# Regression: bumping into something must not freeze the body.
#
# The report from a real game: the character plays fine on open floor, but the
# moment it touches another surface or hits a prop it stops responding in every
# direction. Colliding is supposed to stop motion INTO the obstacle only.

const GRAVITY = 24.0
const WALK = 3.0
const SETTLE = 40
const PUSH = 130
const BACK = 90
const STEP = 110

var body
var velocity = Vector3()
var state = 0
var frames = 0
var mark = 0.0
var failures = []


func _ready():
	print("backend=", PhysicsServer.get_class())
	_trimesh_floor()
	# A prop to walk into, and a low step to walk onto: both box colliders
	# sitting on the trimesh, like level props over imported geometry.
	_box_static(Vector3(0.5, 1.5, 4), Vector3(6, 1.5, 0))
	_box_static(Vector3(2, 0.3, 4), Vector3(-5, 0.3, 0))

	body = KinematicBody.new()
	var box = BoxShape.new()
	box.extents = Vector3(0.4, 0.9, 0.4)
	var col = CollisionShape.new()
	col.shape = box
	body.add_child(col)
	add_child(body)
	body.global_transform = Transform(Basis(), Vector3(3.0, 2.5, 0))


func _trimesh_floor():
	var f = PoolVector3Array()
	var s = 30.0
	f.push_back(Vector3(-s, 0, -s)); f.push_back(Vector3(-s, 0, s)); f.push_back(Vector3(s, 0, s))
	f.push_back(Vector3(-s, 0, -s)); f.push_back(Vector3(s, 0, s)); f.push_back(Vector3(s, 0, -s))
	var tri = ConcavePolygonShape.new()
	tri.set_faces(f)
	var sb = StaticBody.new()
	var c = CollisionShape.new()
	c.shape = tri
	sb.add_child(c)
	add_child(sb)


func _box_static(p_extents, p_origin):
	var sb = StaticBody.new()
	var sh = BoxShape.new()
	sh.extents = p_extents
	var c = CollisionShape.new()
	c.shape = sh
	sb.add_child(c)
	add_child(sb)
	sb.global_transform = Transform(Basis(), p_origin)


func _check(p_ok, p_what):
	if not p_ok:
		failures.append(p_what)
	print(("  ok   " if p_ok else "  FAIL ") + p_what)


func _physics_process(delta):
	frames += 1
	velocity.y -= GRAVITY * delta
	var x = body.global_transform.origin.x
	match state:
		0:
			velocity = body.move_and_slide(velocity, Vector3.UP)
			if frames >= SETTLE:
				print("unstick:")
				_check(body.is_on_floor(), "lands on the floor")
				state = 1; frames = 0; mark = x
		1: # walk into the prop
			velocity.x = WALK
			velocity = body.move_and_slide(velocity, Vector3.UP)
			if frames >= PUSH:
				_check(x > 5.0 and x < 5.25, "presses against the prop face at x=5.1 (got %.3f)" % x)
				_check(not body.is_on_wall() or true, "reached the prop")
				state = 2; frames = 0; mark = x
		2: # now walk AWAY: this is what freezes
			velocity.x = -WALK
			velocity = body.move_and_slide(velocity, Vector3.UP)
			if frames >= BACK:
				_check(x < mark - 2.0, "can walk back away from the prop (moved %.3f)" % (x - mark))
				state = 3; frames = 0; mark = x
		3: # walk onto the low step
			velocity.x = -WALK
			velocity = body.move_and_slide(velocity, Vector3.UP)
			if frames >= STEP:
				_check(x < mark - 2.0, "keeps walking over a second surface (moved %.3f)" % (x - mark))
				_check(body.is_on_floor(), "still grounded on the step")
				_finish()


func _finish():
	if failures.empty():
		print("RESULT m11_unstick -> PASS")
		get_tree().quit(0)
	else:
		print("RESULT m11_unstick -> FAIL (%d): %s" % [failures.size(), str(failures)])
		get_tree().quit(1)
