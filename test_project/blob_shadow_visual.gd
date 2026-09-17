extends SceneTree

# Aceptación visual de BlobShadow (backport PR 84804).
#
# Monta una escena mínima (plano claro, luz direccional con blob shadows,
# caster esférico + BlobFocus), captura un frame con el caster visible y otro
# con el caster oculto y compara la luminancia de la zona de sombra contra una
# zona de control. Requiere un contexto GL real (ver docs/blob-shadow-backport-spec.md):
#
#   xvfb-run -a -s "-screen 0 1024x600x24" \
#     bin/godot.x11.opt.tools.64 --path test_project -s blob_shadow_visual.gd
#
# Imprime BLOB_OK / BLOB_FAIL y sale con 0/1.

const OUT_DIR := "user://blob_shadow_visual"
const SHADOW_FRAMES := 90
const CLEAR_FRAMES := 150

var frames := 0
var caster: BlobShadow
var with_img: Image
var without_img: Image

func _init():
	var dir := Directory.new()
	dir.make_dir_recursive(OUT_DIR)

	var root := Spatial.new()
	get_root().add_child(root)

	var cam := Camera.new()
	cam.translation = Vector3(0, 5, 7)
	cam.rotation_degrees = Vector3(-35, 0, 0)
	cam.current = true
	root.add_child(cam)

	if OS.get_environment("BLOB_SSAO") == "1":
		# SSAO turns on the MRT path in GLES3, where the light is accumulated in
		# diffuse/specular buffers instead of frag_color.
		var env := Environment.new()
		env.background_mode = Environment.BG_COLOR
		env.background_color = Color(0.1, 0.1, 0.12)
		env.ssao_enabled = true
		cam.environment = env

	var ground := MeshInstance.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(20, 20)
	ground.mesh = pm
	var mat := SpatialMaterial.new()
	mat.albedo_color = Color(0.85, 0.85, 0.85)
	ground.material_override = mat
	root.add_child(ground)

	# La luz direccional debe estar por encima de los casters: su AABB de blob
	# shadows cuelga desde su posicion hacia abajo.
	var sun := DirectionalLight.new()
	sun.translation = Vector3(0, 10, 0)
	sun.rotation_degrees = Vector3(-70, 20, 0)
	sun.light_energy = 1.0
	sun.shadow_enabled = false
	sun.blob_shadow_enabled = true
	sun.blob_shadow_shadow_only = true
	root.add_child(sun)

	# Una luz normal para iluminar: la de blob shadows es "shadow only" y no
	# aporta luz. En el caso con SSAO el ambiente por defecto no existe, asi que
	# sin esta luz el plano quedaria negro.
	var key := DirectionalLight.new()
	key.rotation_degrees = Vector3(-50, -35, 0)
	key.light_energy = 1.0
	key.shadow_enabled = false
	root.add_child(key)

	caster = BlobShadow.new()
	caster.translation = Vector3(0, 1.0, 0)
	caster.set_radius(0, 1.0)
	root.add_child(caster)

	var focus := BlobFocus.new()
	focus.translation = Vector3(0, 1.0, 0)
	root.add_child(focus)

	connect("idle_frame", self, "_on_frame")

func _on_frame():
	frames += 1
	if frames == SHADOW_FRAMES:
		with_img = _grab("with")
	elif frames == SHADOW_FRAMES + 1:
		caster.visible = false
	elif frames == CLEAR_FRAMES:
		without_img = _grab("without")
		var ok := _report()
		quit(0 if ok else 1)

func _grab(tag: String) -> Image:
	var img = get_root().get_texture().get_data()
	img.flip_y()
	img.save_png(OUT_DIR.plus_file("blob_" + tag + ".png"))
	img.lock()
	return img

func _mean_luma(img: Image, x0: int, y0: int, x1: int, y1: int) -> float:
	var total := 0.0
	var n := 0
	for y in range(y0, y1):
		for x in range(x0, x1):
			var c = img.get_pixel(x, y)
			total += (c.r + c.g + c.b) / 3.0
			n += 1
	return total / float(n)

func _report() -> bool:
	var s = get_root().get_size()
	var cx = s.x / 2
	var cy = int(s.y * 0.49)
	var shadow_with = _mean_luma(with_img, cx - 25, cy - 25, cx + 25, cy + 25)
	var shadow_without = _mean_luma(without_img, cx - 25, cy - 25, cx + 25, cy + 25)
	var corner_with = _mean_luma(with_img, 20, 20, 60, 60)
	var corner_without = _mean_luma(without_img, 20, 20, 60, 60)
	var delta = shadow_without - shadow_with
	var control = abs(corner_without - corner_with)
	print("BLOB_TEST size=", s, " shadow_with=", shadow_with, " shadow_without=", shadow_without, " delta=", delta, " control_delta=", control)
	if delta > 0.05 and control < 0.02:
		print("BLOB_OK")
		return true
	print("BLOB_FAIL")
	return false
