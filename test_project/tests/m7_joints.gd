extends Spatial

# M7 acceptance: joints through the stock Godot node API.
#   1. A pin joint holds a ball at a fixed distance from an anchor; knocked
#      off its equilibrium it swings back and settles hanging below the pin.
#   2. A hinge joint lets a panel rotate about the hinge axis only.
#   3. A slider joint moves along its axis under gravity (elevator car).

const TOLERANCE = 0.08
const SETTLE_FRAMES = 180

var state = 0
var frames = 0
var failures = []
var ball
var panel
var car

func _ready():
	print("backend=", PhysicsServer.get_class())

	# --- Pin joint pendulum ---------------------------------------------
	var anchor = StaticBody.new()
	anchor.add_child(_shape_col(BoxShape.new(), Vector3(0.1, 0.1, 0.1)))
	add_child(anchor)
	anchor.global_transform = Transform(Basis(), Vector3(-4, 3, 0))

	ball = RigidBody.new()
	ball.add_child(_shape_col(SphereShape.new(), null, 0.25))
	add_child(ball)
	ball.global_transform = Transform(Basis(), Vector3(-3.7, 2.6, 0))

	var pin = PinJoint.new()
	add_child(pin)
	pin.global_transform = Transform(Basis(), Vector3(-4, 3, 0))
	pin.set_node_a(pin.get_path_to(anchor))
	pin.set_node_b(pin.get_path_to(ball))

	# --- Hinge joint ------------------------------------------------------
	var post = StaticBody.new()
	post.add_child(_shape_col(BoxShape.new(), Vector3(0.3, 0.3, 0.3)))
	add_child(post)
	post.global_transform = Transform(Basis(), Vector3(2, 3, 0))

	panel = RigidBody.new()
	panel.add_child(_shape_col(BoxShape.new(), Vector3(0.3, 0.15, 0.1)))
	add_child(panel)
	# Panel starts swung out; gravity pulls it around the hinge.
	panel.global_transform = Transform(Basis(), Vector3(2.6, 3, 0))

	var hinge = HingeJoint.new()
	add_child(hinge)
	hinge.global_transform = Transform(Basis(), Vector3(2, 3, 0))
	hinge.set_node_a(hinge.get_path_to(post))
	hinge.set_node_b(hinge.get_path_to(panel))

	# --- Slider joint elevator car ----------------------------------------
	var rail = StaticBody.new()
	rail.add_child(_shape_col(BoxShape.new(), Vector3(0.05, 2, 0.05)))
	add_child(rail)
	rail.global_transform = Transform(Basis(), Vector3(8, 4, 0))

	car = RigidBody.new()
	car.add_child(_shape_col(BoxShape.new(), Vector3(0.2, 0.2, 0.2)))
	add_child(car)
	car.global_transform = Transform(Basis(), Vector3(8, 4.4, 0))

	# Slide axis is the joint frame's local X; rotate it onto the world Y axis.
	var slider = SliderJoint.new()
	add_child(slider)
	slider.global_transform = Transform(Basis(Vector3(0, 1, 0), Vector3(-1, 0, 0), Vector3(0, 0, 1)), Vector3(8, 4, 0))
	slider.set_node_a(slider.get_path_to(rail))
	slider.set_node_b(slider.get_path_to(car))

func _shape_col(p_shape, p_extents = null, p_radius = 0.0):
	var col = CollisionShape.new()
	col.shape = p_shape
	return col

func _check(p_ok, p_what):
	if not p_ok:
		failures.append(p_what)
	print(("  ok   " if p_ok else "  FAIL ") + p_what)

func _physics_process(delta):
	frames += 1
	if frames < SETTLE_FRAMES:
		return

	print("pin joint:")
	var bp = ball.global_transform.origin
	var anchor_pos = Vector3(-4, 3, 0)
	var distance = bp.distance_to(anchor_pos)
	_check(abs(distance - 0.5) < 0.15, "ball keeps pin distance 0.5 (got %.3f)" % distance)
	_check(abs(bp.x - (-4.0)) < 0.2 and abs(bp.z) < 0.2, "ball settles under the pin (got %s)" % bp)

	print("hinge joint:")
	var pp = panel.global_transform.origin
	# The panel hangs from the hinge: it settles below the hinge post, and its
	# pivot offset (0.6, 0) rotates to face down.
	_check(pp.y < 3.0 and pp.y > 2.4, "panel hangs just under the hinge pivot (y=%.3f)" % pp.y)
	_check(pp.distance_to(Vector3(2, 3, 0)) < 0.75, "panel stays near the hinge")

	print("slider joint:")
	var cp = car.global_transform.origin
	_check(abs(cp.x - 8.0) < 0.3 and abs(cp.z) < 0.05, "car stays on the rail x-axis (got %s)" % cp)
	_check(cp.y < 3.55 and cp.y > 3.25, "car rests at the rail lower limit (y=%.3f)" % cp.y)

	_finish()

func _finish():
	if failures.empty():
		print("RESULT m7_joints -> PASS")
		get_tree().quit(0)
	else:
		print("RESULT m7_joints -> FAIL (%d): %s" % [failures.size(), str(failures)])
		get_tree().quit(1)
