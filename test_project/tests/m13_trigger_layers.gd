extends Spatial

# Regression: an Area must detect a body by Godot's rule, area.mask & body.layer.
# A trigger normally sits on its own layer and the player does NOT carry that
# layer in its mask, because the player has no reason to collide with triggers.

var area
var body
var velocity = Vector3()
var frames = 0
var entered = []
var exited = []
var overlapping_inside = -1
var failures = []


func _ready():
	print("backend=", PhysicsServer.get_class())

	area = Area.new()
	var ashape = BoxShape.new()
	ashape.extents = Vector3(1.5, 1.5, 1.5)
	var acol = CollisionShape.new()
	acol.shape = ashape
	area.add_child(acol)
	add_child(area)
	area.global_transform = Transform(Basis(), Vector3(4, 1, 0))
	# Trigger layer 5 (bit value 16), watching the player layer 2 (value 2).
	area.collision_layer = 16
	area.collision_mask = 2
	area.connect("body_entered", self, "_on_body_entered")
	area.connect("body_exited", self, "_on_body_exited")

	var floor_body = StaticBody.new()
	var fshape = BoxShape.new()
	fshape.extents = Vector3(20, 0.5, 20)
	var fcol = CollisionShape.new()
	fcol.shape = fshape
	floor_body.add_child(fcol)
	add_child(floor_body)
	floor_body.collision_layer = 1
	floor_body.collision_mask = 0

	body = KinematicBody.new()
	var bshape = BoxShape.new()
	bshape.extents = Vector3(0.4, 0.9, 0.4)
	var bcol = CollisionShape.new()
	bcol.shape = bshape
	body.add_child(bcol)
	add_child(body)
	body.global_transform = Transform(Basis(), Vector3(0, 1.5, 0))
	# Player on layer 2, colliding with the world only. NOT with the trigger layer.
	body.collision_layer = 2
	body.collision_mask = 1


func _on_body_entered(p_body):
	entered.append(p_body)


func _on_body_exited(p_body):
	exited.append(p_body)


func _check(p_ok, p_what):
	if not p_ok:
		failures.append(p_what)
	print(("  ok   " if p_ok else "  FAIL ") + p_what)


func _physics_process(delta):
	frames += 1
	velocity.y -= 24.0 * delta
	velocity.x = 3.0
	velocity = body.move_and_slide(velocity, Vector3.UP)
	if frames == 80:
		# Mid-crossing: the elevator prop in the real game reads this API.
		overlapping_inside = area.get_overlapping_bodies().size()
	if frames < 140:
		return

	print("trigger layers:")
	_check(body.global_transform.origin.x > 4.0, "walked through the trigger (x=%.2f)" % body.global_transform.origin.x)
	_check(entered.size() > 0, "body_entered fired (times=%d)" % entered.size())
	if entered.size() > 0:
		_check(entered[0] == body, "reports the player as the body")
	_check(overlapping_inside == 1, "get_overlapping_bodies() sees the body while inside (got %d)" % overlapping_inside)
	_check(exited.size() > 0, "body_exited fired on the way out (times=%d)" % exited.size())

	if failures.empty():
		print("RESULT m13_trigger_layers -> PASS")
		get_tree().quit(0)
	else:
		print("RESULT m13_trigger_layers -> FAIL (%d): %s" % [failures.size(), str(failures)])
		get_tree().quit(1)
