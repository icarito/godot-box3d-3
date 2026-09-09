extends Spatial

# Settled-pile benchmark: the opposite of bench_stress. A thousand boxes are
# dropped, given time to fall asleep, and the physics cost of the SLEEPING
# scene is measured. This is what a level with props costs every frame after
# everything has come to rest.
#   BENCH result engine=<name> avg_physics_ms=<x> active=<y> pairs=<z>

const STACKS = 40
const PER_STACK = 25
const WARMUP_FRAMES = 400
const MEASURE_FRAMES = 300

var frames = 0
var acc_usec = 0
var spawned = 0

func _ready():
	randomize()
	# A big slab so the pile has somewhere to come to rest.
	var floor_body = StaticBody.new()
	var floor_shape = BoxShape.new()
	floor_shape.extents = Vector3(30, 1, 12)
	var floor_col = CollisionShape.new()
	floor_col.shape = floor_shape
	floor_col.translation = Vector3(0, -1, 0)
	floor_body.add_child(floor_col)
	add_child(floor_body)
	for s in range(STACKS):
		var ox = -12.0 + 24.0 * (s % 8) / 7.0
		var oz = -6.0 + 12.0 * (s / 8) / 4.0
		for i in range(PER_STACK):
			var body = RigidBody.new()
			var shape = BoxShape.new()
			shape.extents = Vector3(0.4, 0.4, 0.4)
			var col = CollisionShape.new()
			col.shape = shape
			body.add_child(col)
			var mesh = MeshInstance.new()
			var cube = CubeMesh.new()
			cube.size = Vector3(0.8, 0.8, 0.8)
			mesh.mesh = cube
			body.add_child(mesh)
			add_child(body)
			body.global_transform = Transform(Basis(), Vector3(ox, 0.41 + i * 0.8, oz))
			spawned += 1
	print("BENCH start engine=", ProjectSettings.get_setting("physics/3d/physics_engine"),
			" bodies=", spawned)

func _physics_process(delta):
	frames += 1
	if frames == WARMUP_FRAMES - 30:
		# Sanity line: how much of the pile is still awake right before the window.
		print("BENCH pre-window awake=", PhysicsServer.get_process_info(PhysicsServer.INFO_ACTIVE_OBJECTS))
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
