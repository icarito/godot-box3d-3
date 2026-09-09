extends Spatial

# Regression: a RigidBody must block a KinematicBody walking into it.
#
# The game's pushable crates are RigidBodies that get parked in MODE_KINEMATIC
# once they settle, so both modes have to stop a character. Walking through them
# is what breaks pushing: the character never registers contact, so it never
# enters its push state.

const GRAVITY = 24.0
const WALK_SPEED = 3.0
const FRAMES = 120

var cases = []
var frames = 0
var failures = []


func _ready():
	print("backend=", PhysicsServer.get_class())
	var floor_body = StaticBody.new()
	var fshape = BoxShape.new()
	fshape.extents = Vector3(30, 0.5, 30)
	var fcol = CollisionShape.new()
	fcol.shape = fshape
	floor_body.add_child(fcol)
	add_child(floor_body)
	floor_body.global_transform.origin = Vector3(0, -0.5, 0)

	cases.append(_make_case(0, RigidBody.MODE_RIGID, "dynamic"))
	cases.append(_make_case(10, RigidBody.MODE_KINEMATIC, "kinematic-mode"))
	# The game's crate is authored kinematic, made rigid in _ready, and parked
	# back to kinematic once it settles.
	cases.append(_make_case(20, RigidBody.MODE_KINEMATIC, "mode-switched"))


func _make_case(z, mode, label):
	var crate = RigidBody.new()
	crate.mode = mode
	var cshape = BoxShape.new()
	cshape.extents = Vector3(0.5, 0.5, 0.5)
	var ccol = CollisionShape.new()
	ccol.shape = cshape
	crate.add_child(ccol)
	add_child(crate)
	crate.global_transform.origin = Vector3(3, 0.5, z)

	var walker = KinematicBody.new()
	var shape = BoxShape.new()
	shape.extents = Vector3(0.4, 0.9, 0.4)
	var col = CollisionShape.new()
	col.shape = shape
	walker.add_child(col)
	add_child(walker)
	walker.global_transform.origin = Vector3(0, 1.0, z)
	return {"walker": walker, "crate": crate, "label": label, "vel": Vector3(), "touched": false}


func _check(ok, message):
	if not ok:
		failures.append(message)
	print(("  ok   " if ok else "  FAIL ") + message)


func _physics_process(delta):
	frames += 1
	if frames == 1:
		cases[2].crate.mode = RigidBody.MODE_RIGID
	if frames == 30:
		cases[2].crate.mode = RigidBody.MODE_KINEMATIC
	for c in cases:
		c.vel.y -= GRAVITY * delta
		c.vel.x = WALK_SPEED
		c.vel = c.walker.move_and_slide(c.vel, Vector3.UP)
		for i in range(c.walker.get_slide_count()):
			if c.walker.get_slide_collision(i).collider == c.crate:
				c.touched = true
	if frames < FRAMES:
		return

	print("kinematic vs rigid body:")
	for c in cases:
		var x = c.walker.global_transform.origin.x
		# Crate near face sits at 2.5, walker half-extent 0.4 -> it should stop
		# around 2.1 and never reach the far side.
		_check(x < 2.6, "%s crate stops the walker (x=%.3f)" % [c.label, x])
		_check(c.touched, "%s crate is reported as a collider" % c.label)
	print("RESULT m24_push_block -> ", "PASS" if failures.empty() else "FAIL: " + str(failures))
	get_tree().quit(0 if failures.empty() else 1)
