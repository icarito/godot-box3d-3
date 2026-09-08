extends Spatial

# M6 acceptance: area monitoring and space overrides through the stock Godot API.
#   1. A kinematic body walking through an area triggers body_entered/exited.
#   2. A rigid body falling through the area triggers body_entered.
#   3. A zero gravity override area holds a body at rest.

const GRAVITY = 24.0

var state = 0
var frames = 0
var failures = []
var area
var walker
var rigid
var zero_g
var floater
var walker_entered = 0
var walker_exited = 0
var rigid_entered = 0

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

	# Test area the walker and the rigid body pass through.
	area = Area.new()
	area.monitorable = true
	var area_shape = BoxShape.new()
	area_shape.extents = Vector3(0.5, 1, 0.5)
	var area_col = CollisionShape.new()
	area_col.shape = area_shape
	area.add_child(area_col)
	add_child(area)
	area.global_transform = Transform(Basis(), Vector3(0, 1.5, 0))
	area.connect("body_entered", self, "_on_area_body_entered")
	area.connect("body_exited", self, "_on_area_body_exited")

	# Zero gravity override zone.
	zero_g = Area.new()
	zero_g.gravity = 0.0
	zero_g.space_override = Area.SPACE_OVERRIDE_REPLACE
	var zg_shape = BoxShape.new()
	zg_shape.extents = Vector3(1, 1, 1)
	var zg_col = CollisionShape.new()
	zg_col.shape = zg_shape
	zero_g.add_child(zg_col)
	add_child(zero_g)
	zero_g.global_transform = Transform(Basis(), Vector3(-5, 2.5, 0))

	walker = KinematicBody.new()
	var wcol = CollisionShape.new()
	var wshape = BoxShape.new()
	wshape.extents = Vector3(0.3, 0.3, 0.3)
	wcol.shape = wshape
	walker.add_child(wcol)
	add_child(walker)
	walker.global_transform = Transform(Basis(), Vector3(-4, 1.5, 0))

	rigid = RigidBody.new()
	var rcol = CollisionShape.new()
	var rshape = BoxShape.new()
	rshape.extents = Vector3(0.2, 0.2, 0.2)
	rcol.shape = rshape
	rigid.add_child(rcol)
	add_child(rigid)
	rigid.global_transform = Transform(Basis(), Vector3(0, 4, 0))

	floater = RigidBody.new()
	var fcol = CollisionShape.new()
	var fshape = BoxShape.new()
	fshape.extents = Vector3(0.25, 0.25, 0.25)
	fcol.shape = fshape
	floater.add_child(fcol)
	add_child(floater)
	floater.global_transform = Transform(Basis(), Vector3(-5, 2.5, 0))

func _on_area_body_entered(p_what):
	if p_what == walker:
		walker_entered += 1
	if p_what == rigid:
		rigid_entered += 1

func _on_area_body_exited(p_what):
	if p_what == walker:
		walker_exited += 1

func _check(p_ok, p_what):
	if not p_ok:
		failures.append(p_what)
	print(("  ok   " if p_ok else "  FAIL ") + p_what)

func _physics_process(delta):
	frames += 1
	match state:
		0:
			# Walk the kinematic body through the area.
			walker.move_and_slide(Vector3(8, 0, 0), Vector3.UP)
			if frames >= 60:
				print("walker pos=", walker.global_transform.origin)
				_check(walker_entered > 0, "kinematic walker entered the area (times=%d)" % walker_entered)
				_check(walker_exited > 0, "kinematic walker left the area (times=%d)" % walker_exited)
				state = 1
				frames = 0
		1:
			# Wait for the rigid body to fall through the area.
			if rigid.global_transform.origin.y < 1.2 or frames > 90:
				_check(rigid_entered > 0, "falling rigid body entered the area (times=%d)" % rigid_entered)
				state = 2
				frames = 0
		2:
			# The floater sits inside the zero gravity zone and must not move.
			if frames >= 60:
				print("walker pos=", walker.global_transform.origin)
				var v = floater.linear_velocity
				var moved = floater.global_transform.origin.distance_to(Vector3(-5, 2.5, 0))
				print("space override:")
				_check(v.length() < 0.05 and moved < 0.05,
					"zero gravity override holds the body (v=%.3f moved=%.3f)" % [v.length(), moved])
				_finish()

func _finish():
	if failures.empty():
		print("RESULT m6_areas -> PASS")
		get_tree().quit(0)
	else:
		print("RESULT m6_areas -> FAIL (%d): %s" % [failures.size(), str(failures)])
		get_tree().quit(1)
