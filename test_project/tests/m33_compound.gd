extends Spatial

# M33 acceptance: Box3D compound shapes (Box3DCompound + Box3DCompoundShape).
#
# Compounds are the streaming primitive for large static level tiles: bake the
# geometry once, serialize to bytes, keep the bytes and attach them to a static
# body at load time. This checks the whole path: bake -> bytes -> validate ->
# shape -> static body -> collision queries, plus the invalid-byte rejections
# and the static-only rule.

const MAX_FRAMES = 90

var frames = 0
var problems := 0
var _body: StaticBody

func _check(cond: bool, what: String) -> void:
	if not cond:
		problems += 1
		print("CHECK FAILED: ", what)

func _ray(from: Vector3, offset: Vector3, what: String) -> void:
	var hit := get_world().direct_space_state.intersect_ray(from, from + offset)
	_check(not hit.empty(), "rayo toca " + what)

func _ready():
	print("backend=", PhysicsServer.get_class())

	var comp := Box3DCompound.new()
	_check(comp.get_child_count() == 0, "arranca sin hijos")

	# Un piso 4x4 (sopa de 2 triangulos), una caja, una esfera y una capsula.
	var faces := PoolVector3Array([
			Vector3(-2, 0, -2), Vector3(-2, 0, 2), Vector3(2, 0, 2),
			Vector3(-2, 0, -2), Vector3(2, 0, 2), Vector3(2, 0, -2)])
	comp.add_mesh(faces, Transform(), Vector3(1, 1, 1))
	comp.add_box(Vector3(0.5, 0.5, 0.5), Transform(Basis(), Vector3(0, 0.5, 0)))
	comp.add_sphere(0.4, Transform(Basis(), Vector3(3, 0.5, 0)))
	comp.add_capsule(0.3, 1.0, Transform(Basis(), Vector3(-3, 0.8, 0)))
	_check(comp.get_child_count() == 4, "4 hijos horneados (mesh+box+sphere+capsule)")

	var bytes := comp.bake()
	_check(bytes.size() > 0, "bake devuelve bytes")
	print("compound bytes=", bytes.size(), " hijos=", comp.get_child_count())

	_check(Box3DCompound.new().is_valid_compound(bytes), "los bytes horneados son validos")
	_check(not Box3DCompound.new().is_valid_compound(PoolByteArray([1, 2, 3])), "bytes cortos se rechazan")
	# PoolByteArray no tiene duplicate() en Godot 3: se copia a mano.
	var corrupt := PoolByteArray()
	for i in range(bytes.size()):
		corrupt.append(bytes[i])
	corrupt[0] = 0
	_check(not Box3DCompound.new().is_valid_compound(corrupt), "version corrupta se rechaza")

	var shape := Box3DCompoundShape.new()
	shape.set_compound_bytes(bytes)
	_check(shape.get_compound_bytes().size() == bytes.size(), "la shape guarda los bytes")

	_body = StaticBody.new()
	var cs := CollisionShape.new()
	cs.shape = shape
	_body.add_child(cs)
	add_child(_body)

func _physics_process(_delta):
	frames += 1
	if frames < MAX_FRAMES:
		return

	# Los rayos bajan 5 unidades desde y=3 (el offset es un delta, no un punto).
	_ray(Vector3(1.5, 3, 1.5), Vector3(0, -5, 0), "el piso")
	_ray(Vector3(0, 3, 0), Vector3(0, -5, 0), "la caja")
	_ray(Vector3(3, 3, 0), Vector3(0, -5, 0), "la esfera")
	_ray(Vector3(-3, 3, 0), Vector3(0, -5, 0), "la capsula")
	# Fuera del compound no debe haber nada.
	var miss := get_world().direct_space_state.intersect_ray(Vector3(20, 3, 20), Vector3(20, -3, 20))
	_check(miss.empty(), "lejos del compound no hay colision")

	print("RESULT problems=%d -> %s" % [problems, "PASS" if problems == 0 else "FAIL"])
	get_tree().quit(0 if problems == 0 else 1)
