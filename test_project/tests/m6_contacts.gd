extends Spatial

# M6 acceptance: contact reporting through the stock Godot API.
#   A rigid box with contact monitoring falls onto the floor:
#   - body_entered fires with the floor,
#   - _integrate_forces reports a contact with an upward normal and a collider.

const FLOOR_TOP = 1.0
const TOLERANCE = 0.06
const SETTLE_FRAMES = 90

const ContactBodyScript = preload("res://tests/m6_contact_body.gd")

var body
var entered = []
var exited = []
var frames = 0
var failures = []

func _ready():
	print("backend=", PhysicsServer.get_class())
	var floor_body = StaticBody.new()
	var shape = BoxShape.new()
	shape.extents = Vector3(8, 0.5, 8)
	var col = CollisionShape.new()
	col.shape = shape
	floor_body.add_child(col)
	add_child(floor_body)
	floor_body.global_transform = Transform(Basis(), Vector3(0, 0, 0))

	body = RigidBody.new()
	body.contact_monitor = true
	body.contacts_reported = 4
	body.set_script(ContactBodyScript)
	var box = BoxShape.new()
	box.extents = Vector3(0.5, 0.5, 0.5)
	var body_col = CollisionShape.new()
	body_col.shape = box
	body.add_child(body_col)
	add_child(body)
	body.global_transform = Transform(Basis(), Vector3(0, 4, 0))
	body.connect("body_entered", self, "_on_body_entered")
	body.connect("body_exited", self, "_on_body_exited")

func _on_body_entered(other):
	entered.append(other)

func _on_body_exited(p_what):
	exited.append(p_what)

func _check(p_ok, p_what):
	if not p_ok:
		failures.append(p_what)
	print(("  ok   " if p_ok else "  FAIL ") + p_what)

func _physics_process(delta):
	frames += 1
	if frames < SETTLE_FRAMES:
		return

	print("contact monitoring:")
	_check(abs(body.global_transform.origin.y - FLOOR_TOP) < TOLERANCE, "rests on the floor")
	_check(entered.size() > 0, "body_entered fired (times=%d)" % entered.size())
	if entered.size() > 0:
		_check(entered[0] == get_child(0), "body_entered names the floor")

	print("direct state contacts:")
	if body.has_meta("contacts"):
		var data = body.get_meta("contacts")
		_check(data["count"] >= 1, "direct state reports a contact")
		_check(data["normal"].dot(Vector3.UP) > 0.9, "contact normal points up (got %s)" % data["normal"])
		_check(data["position"].length() < 0.9,
			"contact local position is on the box surface (got %s)" % data["position"])
		_check(data["collider"] == get_child(0).get_rid(), "contact names the floor rid")
		_check(abs(data["collider_position"].y - 0.5) < 0.06,
			"collider position is near the floor top (y=%.3f)" % data["collider_position"].y)
		var g = data["total_gravity"]
		_check(abs(g.y + 9.8) < 0.5, "total gravity is the world gravity (got %s)" % g)
	else:
		_check(false, "direct state never reported contacts")

	_finish()

func _finish():
	if failures.empty():
		print("RESULT m6_contacts -> PASS")
		get_tree().quit(0)
	else:
		print("RESULT m6_contacts -> FAIL (%d): %s" % [failures.size(), str(failures)])
		get_tree().quit(1)
