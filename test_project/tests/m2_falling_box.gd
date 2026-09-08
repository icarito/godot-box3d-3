extends Spatial

# M2 acceptance: a RigidBody box must fall and come to rest on a StaticBody floor.
# Floor top is at y = 0.5, the box is a 1m cube, so it rests with its origin at y = 1.0.

const MAX_FRAMES = 240
const EXPECTED_Y = 1.0
const POSITION_TOLERANCE = 0.05
const REST_SPEED = 0.05

var frames = 0


func _ready():
	print("backend=", PhysicsServer.get_class())


func _physics_process(_delta):
	frames += 1
	var box = $Box
	if frames % 40 == 0:
		print("frame %d y=%.4f" % [frames, box.global_transform.origin.y])
	if frames < MAX_FRAMES:
		return

	var y = box.global_transform.origin.y
	var speed = box.linear_velocity.length()
	var ok = abs(y - EXPECTED_Y) < POSITION_TOLERANCE and speed < REST_SPEED
	print("RESULT y=%.4f expected=%.4f speed=%.4f -> %s" % [y, EXPECTED_Y, speed, "PASS" if ok else "FAIL"])
	get_tree().quit(0 if ok else 1)
