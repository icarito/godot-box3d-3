extends Spatial

# M32 acceptance: PhysicsServer.get_box3d_profile() exposes Box3D's per-phase
# step timings. The point is that the keys exist, are numeric, and that a step
# was actually measured; the values are only meaningful in a profile build.

const MAX_FRAMES = 120

var frames = 0
var problems := 0

func _check(cond: bool, what: String) -> void:
	if not cond:
		problems += 1
		print("CHECK FAILED: ", what)

func _ready():
	print("backend=", PhysicsServer.get_class())

func _physics_process(_delta):
	frames += 1
	if frames < MAX_FRAMES:
		return

	var p = PhysicsServer.get_box3d_profile()
	_check(typeof(p) == TYPE_DICTIONARY, "profile is a Dictionary")
	if typeof(p) == TYPE_DICTIONARY:
		_check(int(p.get("worlds", 0)) >= 1, "at least one world")
		for key in ["step", "collide", "solve", "integrateVelocities", "integratePositions", "sleepIslands"]:
			_check(p.has(key), "profile has '%s'" % key)
			if p.has(key):
				_check(typeof(p[key]) == TYPE_REAL, "profile '%s' is a float" % key)
		_check(float(p.get("step", -1.0)) >= 0.0, "step time is non-negative")
		print("profile step=%.4f collide=%.4f solve=%.4f pairs=%.4f" % [
			float(p.get("step", 0.0)), float(p.get("collide", 0.0)),
			float(p.get("solve", 0.0)), float(p.get("pairs", 0.0))])
	print("RESULT problems=%d -> %s" % [problems, "PASS" if problems == 0 else "FAIL"])
	get_tree().quit(0 if problems == 0 else 1)
