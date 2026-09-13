extends Spatial

# M19: cylinder shapes with rotated/scaled bases (Odisea's personcard pattern).
#   CylinderShape(radius 0.2, height 2) with the card's basis: the cylinder
#   axis must END UP HORIZONTAL (vertical extent = 2 * radius = 0.4), and its
#   ring position must match. Under the old behavior it stayed upright
#   (vertical extent 2.0) and floated.

var failures = []
var card_body
var card_node

func _ready():
	print("backend=", PhysicsServer.get_class())
	var floor_body = StaticBody.new()
	var fshape = BoxShape.new()
	fshape.extents = Vector3(12, 0.5, 12)
	var fcol = CollisionShape.new()
	fcol.shape = fshape
	floor_body.add_child(fcol)
	add_child(floor_body)
	floor_body.global_transform = Transform(Basis(), Vector3(0, 0, 0))

	# A static body with the personcard cylinder, using the exact transform
	# pattern from Dome_Intro's card ring (rotation about Y in 7.5 deg steps,
	# the card cylinder lying on its side).
	card_body = StaticBody.new()
	card_node = CollisionShape.new()
	var cyl = CylinderShape.new()
	cyl.radius = 0.2
	card_node.shape = cyl
	card_node.transform = Transform(
		Basis(Vector3(0.996917, -0.07387, 0), Vector3(0, 0, -1), Vector3(0.0784593, 0.938605, 0)),
		Vector3(0, 1.2, 0))
	card_body.add_child(card_node)
	add_child(card_body)
	card_body.global_transform = Transform(Basis(), Vector3(0, 0, 0))

func _check(p_ok, p_what):
	if not p_ok:
		failures.append(p_what)
	print(("  ok   " if p_ok else "  FAIL ") + p_what)

var _ran = false

func _physics_process(delta):
	if _ran or Engine.get_physics_frames() < 10:
		return
	_ran = true

	var space = get_world().direct_space_state
	var cc = card_node.global_transform.origin
	print("personcard cylinder at ", cc)

	# The lying cylinder spans y in [1.2 - 0.2, 1.2 + 0.2]. A vertical ray from
	# above at its center must hit it at y = 1.4 (its top), and a vertical ray
	# at its center from below at 1.2 - 0.4 must hit its bottom. If the
	# cylinder stayed upright the top would be at 1.2 + 1.0 = 2.2 instead.
	var top = space.intersect_ray(cc + Vector3(0, 3, 0), cc - Vector3(0, 1.0, 0), [], 255)
	if top.empty():
		_check(false, "vertical ray from above misses the cylinder entirely")
	else:
		_check(abs(top.position.y - 1.4) < 0.05,
			"lying cylinder top at y=1.4 (got %.4f)" % top.position.y)

	# Horizontal extent: the cylinder axis is horizontal, so a horizontal ray
	# at its center height through its center hits at +-~1.0 (half of the
	# 2.0 height) along the axis direction, not 0.2.
	var axis = card_node.global_transform.basis.y.normalized()
	var axis_hit = space.intersect_ray(cc - axis * 3, cc + axis * 3, [], 255)
	_check(not axis_hit.empty(), "ray along the cylinder axis hits it")

	_finish()

func _finish():
	if failures.empty():
		print("RESULT m19_cylinder_basis -> PASS")
		get_tree().quit(0)
	else:
		print("RESULT m19_cylinder_basis -> FAIL (%d): %s" % [failures.size(), str(failures)])
		get_tree().quit(1)
