extends Spatial

# M9 acceptance: the Odisea Pilot pattern.
#   - A KinematicBody with a capsule (like the pilot's rotated capsule) must
#     land on a trimesh floor and slide horizontally with move_and_slide.
#   - A shadow-style ray grid starting 1 m above the body (inside the capsule)
#     must pass through the body (add_exception) and hit the floor below.
#
# Floor top is y = 0 (a trimesh plane), player capsule center starts at 2.5.

const GRAVITY = 24.0
const SETTLE_FRAMES = 60

var body
var velocity = Vector3()
var state = 0
var frames = 0
var failures = []
var rays = []
var ray_hits_player = 0

func _ready():
	print("backend=", PhysicsServer.get_class())
	_add_trimesh_floor()

	body = KinematicBody.new()
	body.collision_layer = 2
	body.collision_mask = 79
	var capsule = CapsuleShape.new()
	capsule.radius = 0.5
	capsule.height = 1.8
	var col = CollisionShape.new()
	col.shape = capsule
	# Same orientation as the pilot's CollisionShape in Pilot_v2.tscn.
	var pilot_basis = Basis(Vector3(-1, 0, 0), Vector3(0, 0, -1), Vector3(0, -1, 0))
	col.transform = Transform(pilot_basis, Vector3(0, 1, 0))
	body.add_child(col)
	add_child(body)
	body.global_transform = Transform(Basis(), Vector3(0.3, 2, 0.3))

	# Shadow-style ray grid: starts 1 m above the body origin, inside the capsule.
	for z in range(3):
		for x in range(3):
			var r = RayCast.new()
			r.enabled = true
			r.collision_mask = 1 # Entorno only, like the shadow's floor mask
			r.cast_to = Vector3(0, -7, 0)
			body.add_child(r)
			r.global_transform = Transform(Basis(), Vector3(0.1 + 0.2 * x, 3.0, 0.1 + 0.2 * z))
			r.add_exception(body)
			rays.append(r)

func _add_trimesh_floor():
	var faces = PoolVector3Array()
	var half = 8.0
	# Two CCW triangles viewed from +Y, covering the whole square.
	faces.append(Vector3(-half, 0, -half))
	faces.append(Vector3(half, 0, half))
	faces.append(Vector3(half, 0, -half))
	faces.append(Vector3(-half, 0, -half))
	faces.append(Vector3(-half, 0, half))
	faces.append(Vector3(half, 0, half))
	var patch = StaticBody.new()
	patch.collision_layer = 1
	var trimesh = ConcavePolygonShape.new()
	trimesh.set_faces(faces)
	var col = CollisionShape.new()
	col.shape = trimesh
	patch.add_child(col)
	add_child(patch)
	patch.global_transform = Transform(Basis(), Vector3(0, 0, 0))

func _check(p_ok, p_what):
	if not p_ok:
		failures.append(p_what)
	print(("  ok   " if p_ok else "  FAIL ") + p_what)

func _physics_process(delta):
	frames += 1
	match state:
		0:
			# Land on the trimesh with gravity, then check horizontal motion.
			velocity.y -= GRAVITY * delta
			velocity = body.move_and_slide(velocity, Vector3.UP)
			if frames >= 60:
				var y = body.global_transform.origin.y
				print("trimesh landing:")
				_check(y > 0.2 and y < 0.6, "capsule rests on the trimesh (y=%.4f)" % y)
				_check(body.is_on_floor(), "is_on_floor() on the trimesh")
				state = 1
				frames = 0
		1:
			# Walk horizontally for a second.
			velocity.y = 0 if body.is_on_floor() else velocity.y
			velocity = body.move_and_slide(Vector3(2, velocity.y, 0), Vector3.UP)
			if frames >= 60:
				var pos = body.global_transform.origin
				print("horizontal move:")
				_check(abs(pos.x - 2.3) < 0.5 and abs(pos.z - 0.3) < 0.2,
					"body walks on the trimesh (pos=%s)" % pos)
				_check(body.is_on_floor(), "still on the floor while walking")
				state = 2
				frames = 0
		2:
			# Shadow rays start inside the capsule but must pass through it.
			ray_hits_player = 0
			for r in rays:
				r.force_raycast_update()
				if r.is_colliding():
					var hit_pos = r.get_collision_point()
					# A hit at the ray origin (waist height) means it struck the
					# body's own capsule instead of the floor.
					if hit_pos.y > 1.2:
						ray_hits_player += 1
			print("shadow rays:")
			_check(ray_hits_player == 0, "no shadow ray hits the player's capsule (hits=%d)" % ray_hits_player)
			var any_floor = false
			for r in rays:
				r.force_raycast_update()
				if r.is_colliding() and abs(r.get_collision_point().y) < 0.1:
					any_floor = true
			_check(any_floor, "shadow rays reach the floor")
			_finish()

func _finish():
	if failures.empty():
		print("RESULT m10_game_repro -> PASS")
		get_tree().quit(0)
	else:
		print("RESULT m10_game_repro -> FAIL (%d): %s" % [failures.size(), str(failures)])
		get_tree().quit(1)

