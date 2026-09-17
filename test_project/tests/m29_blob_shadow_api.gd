extends Spatial

# M29 acceptance: BlobShadow / BlobFocus / blob-shadow Light API smoke.
# The feature is render only, so this exercises the scene nodes and the
# VisualServer side (RID lifetime, param round-trip, type switch, visibility)
# without asserting pixels; the dummy rasterizer must carry the API too.

const MAX_FRAMES = 90

var frames = 0
var light: Light
var caster: BlobShadow
var focus: BlobFocus
var problems := 0

func _check(cond: bool, what: String) -> void:
	if not cond:
		problems += 1
		print("CHECK FAILED: ", what)

func _ready():
	print("backend=", PhysicsServer.get_class())

	light = $Light
	light.blob_shadow_enabled = true
	light.blob_shadow_shadow_only = true
	light.set_blob_shadow_param(Light.BLOB_SHADOW_PARAM_RANGE_MAX, 8.0)
	light.set_blob_shadow_param(Light.BLOB_SHADOW_PARAM_INTENSITY, 0.8)
	light.set_blob_shadow_param(Light.BLOB_SHADOW_PARAM_RANGE_HARDNESS, 0.5)
	_check(light.has_blob_shadow(), "light blob shadow enabled")
	_check(abs(light.get_blob_shadow_param(Light.BLOB_SHADOW_PARAM_RANGE_MAX) - 8.0) < 0.001, "range param round-trip")
	_check(abs(light.get_blob_shadow_param(Light.BLOB_SHADOW_PARAM_INTENSITY) - 0.8) < 0.001, "intensity param round-trip")

	caster = BlobShadow.new()
	caster.type = BlobShadow.BLOB_SHADOW_SPHERE
	caster.radius = 1.5
	caster.translation = Vector3(0, 1, 0)
	add_child(caster)
	_check(caster.get_shadow_type() == BlobShadow.BLOB_SHADOW_SPHERE, "sphere type")
	_check(abs(caster.get_radius(0) - 1.5) < 0.001, "radius round-trip")

	focus = BlobFocus.new()
	focus.translation = Vector3(0, 1, 0)
	add_child(focus)

func _process(_delta):
	frames += 1
	caster.translation = Vector3(sin(frames * 0.1), 1, 0)

	if frames == 20:
		# Switch to a capsule and set the offset half.
		caster.type = BlobShadow.BLOB_SHADOW_CAPSULE
		caster.offset = Vector3(0, 1, 0)
		caster.offset_radius = 0.7
		_check(caster.get_shadow_type() == BlobShadow.BLOB_SHADOW_CAPSULE, "capsule type")
		_check(caster.get_offset() == Vector3(0, 1, 0), "offset round-trip")

	if frames == 40:
		# Toggle the light off and on: the blob light RID must survive both.
		light.blob_shadow_enabled = false
		_check(not light.has_blob_shadow(), "light disabled")
		light.blob_shadow_enabled = true
		_check(light.has_blob_shadow(), "light re-enabled")

	if frames == 60:
		caster.visible = false

	if frames < MAX_FRAMES:
		return

	print("RESULT problems=%d -> %s" % [problems, "PASS" if problems == 0 else "FAIL"])
	get_tree().quit(0 if problems == 0 else 1)
