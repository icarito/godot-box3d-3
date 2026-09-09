extends Spatial

# Regression: a RigidBody must rest on a trimesh floor whichever way its
# triangles are wound.
#
# Box3D treats a mesh triangle as one-sided. Sweeps were taught to ignore that
# (sweep_meshes casts triangle by triangle), but a falling RigidBody is resolved
# by the solver, not by a sweep. Level geometry authored for Godot -- CSG shapes
# and imported meshes both -- carries whatever winding it happens to have, so a
# crate landing on it must not fall through.

const FRAMES = 150

var boxes = []
var frames = 0
var failures = []


func _ready():
	print("backend=", PhysicsServer.get_class())
	# Same quad, opposite windings, side by side.
	_floor(_quad(Vector3(-4, 0, -4), Vector3(-4, 0, 4), Vector3(4, 0, 4), Vector3(4, 0, -4)))
	_floor(_quad(Vector3(6, 0, -4), Vector3(14, 0, -4), Vector3(14, 0, 4), Vector3(6, 0, 4)))
	boxes.append({"body": _box(Vector3(0, 2, 0)), "label": "winding A"})
	boxes.append({"body": _box(Vector3(10, 2, 0)), "label": "winding B"})


func _quad(a, b, c, d):
	var faces = PoolVector3Array()
	faces.push_back(a); faces.push_back(b); faces.push_back(c)
	faces.push_back(a); faces.push_back(c); faces.push_back(d)
	return faces


func _floor(faces):
	var shape = ConcavePolygonShape.new()
	shape.set_faces(faces)
	var body = StaticBody.new()
	var col = CollisionShape.new()
	col.shape = shape
	body.add_child(col)
	add_child(body)


func _box(pos):
	var body = RigidBody.new()
	var shape = BoxShape.new()
	shape.extents = Vector3(0.5, 0.5, 0.5)
	var col = CollisionShape.new()
	col.shape = shape
	body.add_child(col)
	add_child(body)
	body.global_transform.origin = pos
	return body


func _check(ok, message):
	if not ok:
		failures.append(message)
	print(("  ok   " if ok else "  FAIL ") + message)


func _physics_process(_delta):
	frames += 1
	if frames < FRAMES:
		return
	print("rigid body on trimesh:")
	for b in boxes:
		var y = b.body.global_transform.origin.y
		_check(abs(y - 0.5) < 0.15, "%s holds the box up (y=%.3f, want 0.5)" % [b.label, y])
	print("RESULT m25_rigid_trimesh_faces -> ", "PASS" if failures.empty() else "FAIL: " + str(failures))
	get_tree().quit(0 if failures.empty() else 1)
