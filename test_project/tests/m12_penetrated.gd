extends Spatial

# Regression: a body that starts overlapping geometry must push out and then
# move normally. Reported from a real game as "the moment I touch another
# surface or hit something I get stuck", in every direction at once.

const GRAVITY = 24.0
const WALK = 3.0
const FREE = 60
const WALKF = 90

var body
var velocity = Vector3()
var state = 0
var frames = 0
var mark = 0.0
var failures = []


func _ready():
	print("backend=", PhysicsServer.get_class())
	_trimesh_floor()
	# A block the body is spawned INSIDE of, overlapping by 0.3 m.
	_box_static(Vector3(1.0, 1.0, 1.0), Vector3(0, 1.0, 0))

	body = KinematicBody.new()
	var box = BoxShape.new()
	box.extents = Vector3(0.4, 0.9, 0.4)
	var col = CollisionShape.new()
	col.shape = box
	body.add_child(col)
	add_child(body)
	# Overlapping the block on purpose.
	body.global_transform = Transform(Basis(), Vector3(1.1, 1.5, 0))


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
	var o = body.global_transform.origin
	match state:
		0:
			velocity = body.move_and_slide(velocity, Vector3.UP)
			if frames >= FREE:
				print("penetrated start:")
				_check(o.x > 1.35, "pushed out of the block (x=%.3f, needs > 1.35)" % o.x)
				_check(body.is_on_floor(), "reaches the floor")
				state = 1; frames = 0; mark = o.x
		1:
			velocity.x = WALK
			velocity = body.move_and_slide(velocity, Vector3.UP)
			if frames >= WALKF:
				_check(o.x > mark + 2.0, "walks away afterwards (moved %.3f)" % (o.x - mark))
				if failures.empty():
					print("RESULT m12_penetrated -> PASS")
					get_tree().quit(0)
				else:
					print("RESULT m12_penetrated -> FAIL (%d): %s" % [failures.size(), str(failures)])
					get_tree().quit(1)
