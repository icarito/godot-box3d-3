extends Spatial

# Capsule characters must rest steadily against props and keep their tangent
# motion along railings. Both are guarantees of Box3D's mover API.

var pod_walker
var rail_walker
var pod_velocity = Vector3()
var rail_velocity = Vector3()
var frame = 0
var pod_min_x = INF
var pod_max_x = -INF
var failures = []


func _ready():
	_floor()
	_box(Vector3(0.5, 1.4, 0.5), Vector3(0, 1.4, 0))
	_box(Vector3(0.15, 1.4, 10), Vector3(6, 1.4, 0))
	pod_walker = _capsule(Vector3(-3, 1.0, 0))
	rail_walker = _capsule(Vector3(3, 1.0, -5))


func _capsule(position):
	var body = KinematicBody.new()
	var shape = CapsuleShape.new()
	shape.radius = 0.5
	shape.height = 1.0
	var collision = CollisionShape.new()
	collision.shape = shape
	# Godot 3 scenes rotate Bullet's Z capsule axis onto world Y.
	collision.transform = Transform(Vector3(-1, 0, 0), Vector3(0, 0, -1), Vector3(0, -1, 0), Vector3())
	body.add_child(collision)
	add_child(body)
	body.global_transform.origin = position
	return body


func _floor():
	_box(Vector3(20, 0.5, 20), Vector3(0, -0.5, 0))


func _box(extents, position):
	var body = StaticBody.new()
	var shape = BoxShape.new()
	shape.extents = extents
	var collision = CollisionShape.new()
	collision.shape = shape
	body.add_child(collision)
	add_child(body)
	body.global_transform.origin = position


func _check(ok, message):
	if not ok:
		failures.append(message)
	print(("  ok   " if ok else "  FAIL ") + message)


func _physics_process(delta):
	frame += 1
	pod_velocity.y -= 24.0 * delta
	rail_velocity.y -= 24.0 * delta
	if frame > 30:
		pod_velocity.x = 3.0
		rail_velocity.x = 3.0
		rail_velocity.z = 3.0
	# Exactly how Pilot_V2 moves: snapped to the floor and stopping on slopes.
	# Plain move_and_slide() misses both the snap probe and Godot's cancel-sliding
	# path, which is where a capsule against a prop actually starts to buzz.
	var snap = Vector3.DOWN * 0.25 if pod_velocity.y <= 0 else Vector3.ZERO
	pod_velocity = pod_walker.move_and_slide_with_snap(pod_velocity, snap, Vector3.UP, true, 4, deg2rad(45), false)
	snap = Vector3.DOWN * 0.25 if rail_velocity.y <= 0 else Vector3.ZERO
	rail_velocity = rail_walker.move_and_slide_with_snap(rail_velocity, snap, Vector3.UP, true, 4, deg2rad(45), false)

	if frame >= 100:
		pod_min_x = min(pod_min_x, pod_walker.global_transform.origin.x)
		pod_max_x = max(pod_max_x, pod_walker.global_transform.origin.x)
	if frame < 180:
		return

	print("character mover:")
	_check(pod_max_x - pod_min_x < 0.005, "capsule stays steady against a prop (jitter=%.6f)" % (pod_max_x - pod_min_x))
	_check(rail_walker.global_transform.origin.z > 2.0, "capsule slides along a railing (z=%.3f)" % rail_walker.global_transform.origin.z)
	_check(rail_walker.global_transform.origin.x < 5.5, "railing still blocks inward motion (x=%.3f)" % rail_walker.global_transform.origin.x)
	print("RESULT m21_character_mover -> ", "PASS" if failures.empty() else "FAIL: " + str(failures))
	get_tree().quit(0 if failures.empty() else 1)
