extends Spatial

# Regression: an Area whose shape resource is edited after the area exists must
# pick the change up. Zone components routinely duplicate their BoxShape and
# then write extents onto it in _ready(), which is a shape_set_data() on a
# resource the area already holds.

var area
var body
var velocity = Vector3()
var frames = 0
var entered = []
var failures = []


func _ready():
	print("backend=", PhysicsServer.get_class())

	# Grown area: starts tiny, resized big. Only a rebuilt area can reach the body.
	area = _zone(Vector3(3, 2.2, 0), Vector3(0.2, 0.2, 0.2), Vector3(1.5, 1.5, 1.5), "grown")
	# Shrunk area: starts big, resized small and lifted out of reach. A stale
	# area would keep the big box and fire anyway, so this is the discriminating
	# half: it can only pass if shape_set_data reached the area.
	_zone(Vector3(6, 2.2, 0), Vector3(1.5, 1.5, 1.5), Vector3(0.2, 0.2, 0.2), "shrunk")

	var unused_shape = BoxShape.new()
	var floor_body = StaticBody.new()
	var fshape = BoxShape.new()
	fshape.extents = Vector3(20, 0.5, 20)
	var fcol = CollisionShape.new()
	fcol.shape = fshape
	floor_body.add_child(fcol)
	add_child(floor_body)

	body = KinematicBody.new()
	var bshape = BoxShape.new()
	bshape.extents = Vector3(0.3, 0.3, 0.3)
	var bcol = CollisionShape.new()
	bcol.shape = bshape
	body.add_child(bcol)
	add_child(body)
	body.global_transform = Transform(Basis(), Vector3(0, 2.5, 0))


func _zone(p_origin, p_start_extents, p_final_extents, p_tag):
	var zone = Area.new()
	var shape = BoxShape.new()
	shape.extents = p_start_extents
	var col = CollisionShape.new()
	col.shape = shape
	zone.add_child(col)
	add_child(zone)
	zone.global_transform = Transform(Basis(), p_origin)
	zone.connect("body_entered", self, "_on_body_entered", [p_tag])
	# The zone component pattern: edit the shape resource once the area is live.
	shape.extents = p_final_extents
	return zone


func _on_body_entered(p_body, p_tag):
	entered.append(p_tag)


func _check(p_ok, p_what):
	if not p_ok:
		failures.append(p_what)
	print(("  ok   " if p_ok else "  FAIL ") + p_what)


func _physics_process(delta):
	frames += 1
	velocity.y -= 24.0 * delta
	if frames > 40: # let it land first
		velocity.x = 3.0
	velocity = body.move_and_slide(velocity, Vector3.UP)
	if frames < 180:
		return
	print("area reshape:")
	_check(body.global_transform.origin.x > 4.5, "walked past the zone (x=%.2f)" % body.global_transform.origin.x)
	_check(entered.has("grown"), "area resized bigger fires (entered=%s)" % str(entered))
	_check(not entered.has("shrunk"), "area resized smaller no longer fires (entered=%s)" % str(entered))
	if failures.empty():
		print("RESULT m16_area_reshape -> PASS")
		get_tree().quit(0)
	else:
		print("RESULT m16_area_reshape -> FAIL (%d): %s" % [failures.size(), str(failures)])
		get_tree().quit(1)
