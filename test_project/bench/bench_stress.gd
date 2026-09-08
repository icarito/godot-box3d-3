extends Spatial

# Physics benchmark: identical stress scene under every backend.
# Stacks of boxes settle, then a rain of spheres hits them while
# Performance.TIME_PHYSICS_PROCESS is accumulated over the measurement window.
# Prints one line for scripts/bench.sh to parse:
#   BENCH result engine=<name> avg_physics_ms=<x> active=<y> pairs=<z>

const BOXES_PER_STACK = 12
const STACKS = 12
const RAIN_BOXES = 120
const WARMUP_FRAMES = 120
const MEASURE_FRAMES = 300

var frames = 0
var acc_usec = 0
var spawned = 0

func _ready():
	randomize()
	for s in range(STACKS):
		for i in range(BOXES_PER_STACK):
			_add_box(Vector3(-6 + 3 * (s % 5) + (0.02 * i if i % 2 == 0 else -0.02 * i),
					0.51 + i * 1.02, -2.4 + 1.6 * (s / 5)))
	for i in range(RAIN_BOXES):
		_add_box(Vector3(fmod(i * 0.737, 10.0) - 5.0, 14.0 + i * 0.65, fmod(i * 1.311, 6.0) - 3.0))
	print("BENCH start engine=", ProjectSettings.get_setting("physics/3d/physics_engine"),
			" bodies=", spawned)

func _add_box(p_origin):
	var body = RigidBody.new()
	body.can_sleep = false
	var shape = BoxShape.new()
	shape.extents = Vector3(0.5, 0.5, 0.5)
	var col = CollisionShape.new()
	col.shape = shape
	body.add_child(col)
	var mesh = MeshInstance.new()
	var cube = CubeMesh.new()
	cube.size = Vector3(1, 1, 1)
	mesh.mesh = cube
	body.add_child(mesh)
	add_child(body)
	body.global_transform = Transform(Basis(), p_origin)
	spawned += 1

func _physics_process(delta):
	frames += 1
	if frames <= WARMUP_FRAMES:
		return
	acc_usec += Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1000000.0
	if frames == WARMUP_FRAMES + MEASURE_FRAMES:
		var measured = frames - WARMUP_FRAMES
		print("BENCH result engine=", ProjectSettings.get_setting("physics/3d/physics_engine"),
				" avg_physics_ms=", stepify(acc_usec / 1000.0 / measured, 0.01),
				" active=", PhysicsServer.get_process_info(PhysicsServer.INFO_ACTIVE_OBJECTS),
				" pairs=", PhysicsServer.get_process_info(PhysicsServer.INFO_COLLISION_PAIRS))
		get_tree().quit(0)
