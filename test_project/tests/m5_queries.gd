extends Spatial

# M5 acceptance: PhysicsDirectSpaceState queries through the stock Godot API.
#   Floor top is y = 0.5 (center x=2), wall face is x = 4.5 (center x=5).
#   Queries run after one physics frame: that is when CollisionObject nodes
#   have pushed their transforms to the server, same as in real games.

var failures = []

func _ready():
	print("backend=", PhysicsServer.get_class())
	var floor_body = StaticBody.new()
	var floor_shape = BoxShape.new()
	floor_shape.extents = Vector3(12, 0.5, 12)
	var col = CollisionShape.new()
	col.shape = floor_shape
	floor_body.add_child(col)
	add_child(floor_body)
	floor_body.global_transform = Transform(Basis(), Vector3(2, 0, 0))

	var wall = StaticBody.new()
	var wall_shape = BoxShape.new()
	wall_shape.extents = Vector3(0.5, 2, 10)
	var wall_col = CollisionShape.new()
	wall_col.shape = wall_shape
	wall.add_child(wall_col)
	add_child(wall)
	wall.global_transform = Transform(Basis(), Vector3(5, 2, 0))

	var area = Area.new()
	var area_shape = BoxShape.new()
	area_shape.extents = Vector3(0.5, 0.5, 0.5)
	var area_col = CollisionShape.new()
	area_col.shape = area_shape
	area.add_child(area_col)
	add_child(area)
	area.global_transform = Transform(Basis(), Vector3(10, 2, 0))

	yield(get_tree(), "physics_frame")
	_run_queries(floor_body, wall, area)

func _run_queries(p_floor, p_wall, p_area):
	var sphere = SphereShape.new()
	sphere.radius = 0.5
	var box = BoxShape.new()
	box.extents = Vector3(0.25, 0.25, 0.25)

	var space = get_world().direct_space_state

	print("intersect_ray:")
	var result = space.intersect_ray(Vector3(2, 4, 0), Vector3(2, -2, 0))
	_check(not result.empty(), "ray down hits something")
	if not result.empty():
		_check(result.collider == p_floor, "names the floor as the collider")
		_check(abs(result.position.y - 0.5) < 0.01, "hits the floor top (y=%.3f)" % result.position.y)
		_check(result.normal.dot(Vector3.UP) > 0.99, "normal points up (got %s)" % result.normal)
	var miss = space.intersect_ray(Vector3(2, 4, 20), Vector3(2, -2, 20))
	_check(miss.empty(), "ray over the edge misses")

	print("intersect_point:")
	_check(not space.intersect_point(Vector3(5, 2, 0)).empty(), "point inside the wall hits")
	_check(space.intersect_point(Vector3(0, 3, 0)).empty(), "point in free space misses")

	print("intersect_shape:")
	var params = PhysicsShapeQueryParameters.new()
	params.set_shape(sphere)
	params.set_transform(Transform(Basis(), Vector3(4.7, 1.5, 0)))
	var hits = space.intersect_shape(params, 8)
	_check(hits.size() == 1, "sphere overlapping the wall reports one hit (got %d)" % hits.size())
	if hits.size() > 0:
		_check(hits[0].collider == p_wall, "names the wall as the collider")
	params.set_transform(Transform(Basis(), Vector3(0, 5, 0)))
	var no_hits = space.intersect_shape(params, 8)
	_check(no_hits.empty(), "sphere in free space reports none")

	print("cast_motion:")
	var cast = PhysicsShapeQueryParameters.new()
	cast.set_shape(box)
	cast.set_transform(Transform(Basis(), Vector3(2, 2, 0)))
	var fractions = space.cast_motion(cast, Vector3(0, -2, 0))
	_check(fractions.size() == 2, "drop onto the floor returns fractions")
	if fractions.size() == 2:
		_check(fractions[0] > 0.0 and fractions[0] < 1.0, "drop stops early (safe=%.3f)" % fractions[0])
		_check(fractions[1] >= fractions[0] and fractions[1] <= 1.0, "unsafe fraction is ordered (unsafe=%.3f)" % fractions[1])
	var free_fractions = space.cast_motion(cast, Vector3(0, 2, 0))
	_check(free_fractions.size() == 2 and free_fractions[0] >= 0.999,
		"cast into free space is fully safe (safe=%s)" % str(free_fractions))

	print("collide_shape:")
	cast.set_transform(Transform(Basis(), Vector3(2, 0.65, 0)))
	var contact_points = space.collide_shape(cast, 8)
	_check(contact_points.size() >= 2, "box overlapping the floor reports a point pair (got %d)" % contact_points.size())
	cast.set_transform(Transform(Basis(), Vector3(2, 4, 0)))
	_check(space.collide_shape(cast, 8).empty(), "box in free space does not collide")

	print("rest_info:")
	cast.set_transform(Transform(Basis(), Vector3(2, 1.0, 0)))
	cast.set_margin(0.6)
	var rest = space.get_rest_info(cast)
	_check(not rest.empty(), "rest info finds the floor under the box")
	if not rest.empty():
		_check(rest.normal.dot(Vector3.UP) > 0.99, "rest normal points up (got %s)" % rest.normal)
		_check(rest.rid == p_floor.get_rid(), "rest info names the floor")

	print("area queries:")
	_check(not space.intersect_point(Vector3(10, 2, 0), 32, [], 0x7FFFFFFF, true, true).empty(),
		"areas are found when collide_with_areas is set")
	_check(space.intersect_point(Vector3(10, 2, 0), 32, [], 0x7FFFFFFF, true, false).empty(),
		"areas are skipped when collide_with_areas is false")

	_finish()

func _check(p_ok, p_what):
	if not p_ok:
		failures.append(p_what)
	print(("  ok   " if p_ok else "  FAIL ") + p_what)

func _finish():
	if failures.empty():
		print("RESULT m5_queries -> PASS")
		get_tree().quit(0)
	else:
		print("RESULT m5_queries -> FAIL (%d): %s" % [failures.size(), str(failures)])
		get_tree().quit(1)
