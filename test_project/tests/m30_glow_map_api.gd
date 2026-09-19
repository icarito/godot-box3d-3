extends Spatial

# M30 acceptance: Environment glow-map API smoke (glow_map / glow_map_strength,
# backport PR #93133). The effect is GLES3 render only, so this exercises the
# resource setters, the property round-trip and the VisualServer binding without
# asserting pixels; the dummy rasterizer (server build) and GLES2 must carry the
# API too, they just ignore it.

const MAX_FRAMES = 90

var frames = 0
var env: Environment
var tex: ImageTexture
var problems := 0

func _check(cond: bool, what: String) -> void:
	if not cond:
		problems += 1
		print("CHECK FAILED: ", what)

func _ready():
	print("backend=", PhysicsServer.get_class())

	# The entry point is bound on VisualServer whatever the rasterizer is.
	_check(VisualServer.has_method("environment_set_glow_map"), "VisualServer.environment_set_glow_map bound")

	env = Environment.new()
	env.glow_enabled = true

	# Resource defaults.
	_check(env.get_glow_map() == null, "no glow map by default")
	_check(abs(env.get_glow_map_strength() - 0.8) < 0.001, "default glow_map_strength 0.8")

	# Round-trip a texture and a strength through the resource setters.
	var img := Image.new()
	img.create(8, 8, false, Image.FORMAT_RGB8)
	img.fill(Color(1, 1, 1))
	tex = ImageTexture.new()
	tex.create_from_image(img, 0)
	env.glow_map = tex
	env.glow_map_strength = 0.35
	_check(env.get_glow_map() == tex, "glow_map round-trip")
	_check(abs(env.get_glow_map_strength() - 0.35) < 0.001, "glow_map_strength round-trip")
	# The setters already route through VS::environment_set_glow_map (the RID is
	# not exposed to script); has_method above covers the binding itself.

func _process(_delta):
	frames += 1

	if frames == 30:
		# Clearing the map must be allowed and leave the strength member intact.
		env.glow_map = null
		_check(env.get_glow_map() == null, "glow map cleared")
		_check(abs(env.get_glow_map_strength() - 0.35) < 0.001, "strength survives map clear")

	if frames == 60:
		# Reassign.
		env.glow_map = tex
		_check(env.get_glow_map() == tex, "glow map reassigned")

	if frames < MAX_FRAMES:
		return

	print("RESULT problems=%d -> %s" % [problems, "PASS" if problems == 0 else "FAIL"])
	get_tree().quit(0 if problems == 0 else 1)