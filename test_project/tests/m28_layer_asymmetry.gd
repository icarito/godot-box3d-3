extends Spatial

# Regression: Godot's collision rule is OR, not AND.
#
# Two bodies collide when either one's mask sees the other's layer. Box3D's own
# filter is bidirectional -- it wants both directions to agree -- so a prop on
# its own layer fell through a floor whose mask only covers layer 1, which is
# how nearly every level is set up.

const FRAMES = 150
var crate
var frames = 0
var failures = []


func _ready():
	print("backend=", PhysicsServer.get_class())
	var floor_body = StaticBody.new()
	floor_body.collision_layer = 1
	floor_body.collision_mask = 1
	var fshape = BoxShape.new()
	fshape.extents = Vector3(20, 0.5, 20)
	var fcol = CollisionShape.new()
	fcol.shape = fshape
	floor_body.add_child(fcol)
	add_child(floor_body)
	floor_body.global_transform.origin = Vector3(0, -0.5, 0)

	# The game's pushable crate: layers 3 and 7, mask covering 1, 2, 3 and 7.
	crate = RigidBody.new()
	crate.collision_layer = 68
	crate.collision_mask = 71
	var shape = BoxShape.new()
	shape.extents = Vector3(0.5, 0.5, 0.5)
	var col = CollisionShape.new()
	col.shape = shape
	crate.add_child(col)
	add_child(crate)
	crate.global_transform.origin = Vector3(0, 2, 0)


func _check(ok, message):
	if not ok:
		failures.append(message)
	print(("  ok   " if ok else "  FAIL ") + message)


func _physics_process(_delta):
	frames += 1
	if frames < FRAMES:
		return
	var y = crate.global_transform.origin.y
	print("asymmetric layers:")
	_check(abs(y - 0.5) < 0.15, "crate rests on a floor whose mask ignores its layer (y=%.3f, want 0.5)" % y)
	print("RESULT m28_layer_asymmetry -> ", "PASS" if failures.empty() else "FAIL: " + str(failures))
	get_tree().quit(0 if failures.empty() else 1)
