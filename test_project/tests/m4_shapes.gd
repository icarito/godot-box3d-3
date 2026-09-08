extends Spatial

# M4 acceptance: shapes and collision exceptions through the stock Godot API.
#   1. A rigid sphere rests on the floor at its radius.
#   2. A rigid capsule rests at its half height.
#   3. A rigid cylinder rests at its half height.
#   4. A rigid box slides down a convex polygon ramp.
#   5. A sphere rests on a concave polygon (trimesh) patch.
#   6. Collision exceptions: two excluded rigid bodies pass through each other.
#
# All bodies are dropped from y = 3 and given ~1.5 s to settle.

const TOLERANCE = 0.06
const SETTLE_FRAMES = 90
const DROP_Y = 3.0

var state = 0
var frames = 0
var failures = []
var subjects = {}

func _ready():
	print("backend=", PhysicsServer.get_class())
	_add_floor()

	# 1. Sphere: rests at y = 0.9 (floor top 0.5 + radius 0.4).
	var sphere = SphereShape.new()
	sphere.radius = 0.4
	subjects.sphere = _add_rigid(sphere, Vector3(-3, DROP_Y, 0))
	# 2. Capsule: radius 0.25, mid height 0.5 -> rests at y = 1.0.
	var capsule = CapsuleShape.new()
	capsule.radius = 0.25
	capsule.height = 0.5
	subjects.capsule = _add_rigid(capsule, Vector3(-1, DROP_Y, 0))
	# 3. Cylinder: radius 0.25, height 0.6 -> rests at y = 0.8.
	var cylinder = CylinderShape.new()
	cylinder.radius = 0.25
	cylinder.height = 0.6
	subjects.cylinder = _add_rigid(cylinder, Vector3(1, DROP_Y, 0))
	# 4. Box on a convex ramp: slides off, rests on the floor.
	var ramp_box_shape = BoxShape.new()
	ramp_box_shape.extents = Vector3(0.15, 0.15, 0.15)
	subjects.ramp_box = _add_rigid(ramp_box_shape, Vector3(3.5, DROP_Y, 0))
	_add_convex_ramp(Vector3(3.5, 1.2, 0))
	# 5b. Flat convex plate: a zero-volume ConvexPolygonShape, which Godot allows
	# and Box3D's hull builder rejects unless it is thickened first. A static
	# plate at y=2 must still stop a box dropped onto it.
	var plate_points = PoolVector3Array([
		Vector3(-0.5, 0, -0.5), Vector3(0.5, 0, -0.5),
		Vector3(0.5, 0, 0.5), Vector3(-0.5, 0, 0.5)])
	var plate_shape = ConvexPolygonShape.new()
	plate_shape.points = plate_points
	var plate = StaticBody.new()
	var plate_col = CollisionShape.new()
	plate_col.shape = plate_shape
	plate.add_child(plate_col)
	add_child(plate)
	plate.global_transform = Transform(Basis(), Vector3(-3.5, 2.0, 0))
	var plate_box = BoxShape.new()
	plate_box.extents = Vector3(0.2, 0.2, 0.2)
	subjects.plate_box = _add_rigid(plate_box, Vector3(-3.5, DROP_Y, 0))

	# 5. Sphere on a trimesh patch.
	var tri_sphere = SphereShape.new()
	tri_sphere.radius = 0.3
	subjects.trimesh_sphere = _add_rigid(tri_sphere, Vector3(6, DROP_Y, 0))
	_add_trimesh_patch(Vector3(6, 0.9, 0))
	# 6. Exception pair: A drops through B; both end up on the floor.
	var ex_shape = BoxShape.new()
	ex_shape.extents = Vector3(0.3, 0.3, 0.3)
	subjects.ex_a = _add_rigid(ex_shape, Vector3(9, DROP_Y, 0))
	subjects.ex_b = _add_rigid(ex_shape, Vector3(9, 1.0, 0))
	subjects.ex_a.add_collision_exception_with(subjects.ex_b)

func _add_floor():
	var floor_body = StaticBody.new()
	var shape = BoxShape.new()
	shape.extents = Vector3(12, 0.5, 12)
	var col = CollisionShape.new()
	col.shape = shape
	floor_body.add_child(col)
	add_child(floor_body)
	floor_body.global_transform = Transform(Basis(), Vector3(2, 0, 0))

func _add_rigid(p_shape, p_origin):
	var body = RigidBody.new()
	body.contact_monitor = false
	var col = CollisionShape.new()
	col.shape = p_shape
	body.add_child(col)
	add_child(body)
	body.global_transform = Transform(Basis(), p_origin)
	return body

func _add_convex_ramp(p_origin):
	# A wedge: thin plate tilted ~30 degrees, as a convex polygon shape.
	var points = PoolVector3Array()
	var hx = 0.5
	var hy = 0.03
	var hz = 0.8
	var slope = 0.4
	var corners = [
		Vector3(-hx, hy + slope * hx, -hz), Vector3(hx, hy - slope * hx, -hz),
		Vector3(hx, -hy - slope * hx, -hz), Vector3(-hx, -hy + slope * hx, -hz),
		Vector3(-hx, hy + slope * hx, hz), Vector3(hx, hy - slope * hx, hz),
		Vector3(hx, -hy - slope * hx, hz), Vector3(-hx, -hy + slope * hx, hz),
	]
	for p in corners:
		points.append(p)
	var convex = ConvexPolygonShape.new()
	convex.points = points
	var body = StaticBody.new()
	var col = CollisionShape.new()
	col.shape = convex
	body.add_child(col)
	add_child(body)
	body.global_transform = Transform(Basis(), p_origin)

func _add_trimesh_patch(p_origin):
	# Two triangles forming a flat square, wound so the face normal is up:
	# a trimesh is one-sided, like Godot's.
	var faces = PoolVector3Array([
		Vector3(-0.5, 0, -0.5), Vector3(0.5, 0, 0.5), Vector3(0.5, 0, -0.5),
		Vector3(-0.5, 0, -0.5), Vector3(-0.5, 0, 0.5), Vector3(0.5, 0, 0.5),
	])
	var trimesh = ConcavePolygonShape.new()
	trimesh.set_faces(faces)
	var body = StaticBody.new()
	var col = CollisionShape.new()
	col.shape = trimesh
	body.add_child(col)
	add_child(body)
	body.global_transform = Transform(Basis(), p_origin)

func _check(p_ok, p_what):
	if not p_ok:
		failures.append(p_what)
	print(("  ok   " if p_ok else "  FAIL ") + p_what)

func _physics_process(delta):
	frames += 1
	if state == 0:
		if frames >= SETTLE_FRAMES:
			print("shapes:")
			_check(abs(subjects.sphere.global_transform.origin.y - 0.9) < TOLERANCE,
				"sphere rests at y=0.9 (got %.4f)" % subjects.sphere.global_transform.origin.y)
			_check(abs(subjects.capsule.global_transform.origin.y - 1.0) < TOLERANCE,
				"capsule rests at y=1.0 (got %.4f)" % subjects.capsule.global_transform.origin.y)
			_check(abs(subjects.cylinder.global_transform.origin.y - 0.8) < TOLERANCE,
				"cylinder rests at y=0.8 (got %.4f)" % subjects.cylinder.global_transform.origin.y)
			var rb = subjects.ramp_box
			_check(rb.global_transform.origin.y < 1.0 and rb.global_transform.origin.y > 0.2,
				"box leaves the convex ramp (y=%.4f)" % rb.global_transform.origin.y)
			_check(abs(subjects.trimesh_sphere.global_transform.origin.y - 1.2) < TOLERANCE,
				"sphere rests on trimesh at y=1.2 (got %.4f)" % subjects.trimesh_sphere.global_transform.origin.y)
			var pb = subjects.plate_box.global_transform.origin.y
			_check(pb > 1.9, "flat convex plate stops the box (y=%.4f, would fall past 1.9)" % pb)
			print("exceptions:")
			var a = subjects.ex_a.global_transform.origin
			var b = subjects.ex_b.global_transform.origin
			_check(abs(a.y - 0.8) < TOLERANCE and abs(b.y - 0.8) < TOLERANCE,
				"excluded bodies both reach the floor (a=%.3f b=%.3f)" % [a.y, b.y])
			_finish()

func _finish():
	if failures.empty():
		print("RESULT m4_shapes -> PASS")
		get_tree().quit(0)
	else:
		print("RESULT m4_shapes -> FAIL (%d): %s" % [failures.size(), str(failures)])
		get_tree().quit(1)
