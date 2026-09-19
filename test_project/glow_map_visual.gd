extends SceneTree

# Aceptación visual del glow map (backport PR #93133).
#
# Monta una escena mínima (fondo oscuro + un quad emisivo) con glow fuerte,
# captura un frame con `glow_map` (un mapa negro, strength 1.0: el shader mezcla
# el glow con el mapa, así que lo apaga) y otro sin mapa, y compara la
# luminancia del halo alrededor del emisor contra una zona de control. Requiere
# un contexto GL real y el rasterizador GLES3:
#
#   xvfb-run -a -s "-screen 0 1024x600x24" \
#     ../godot/bin/godot.x11.opt.tools.64 --path test_project \
#     --video-driver GLES3 -s glow_map_visual.gd
#
# GLES3 imprime GLOW_OK cuando el halo se apaga con el mapa. GLES2 no
# implementa glow map (el `environment_set_glow_map` es un no-op), así que con
# `--video-driver GLES2` el test espera que con y sin mapa sean iguales e
# imprime GLOW_GLES2_OK. Sale con 0/1.

const OUT_DIR := "user://glow_map_visual"
const MAPPED_FRAMES := 120
const CLEAR_FRAMES := 200

var frames := 0
var env: Environment
var mapped_img: Image
var clear_img: Image
var driver := 0

func _init():
	driver = OS.get_current_video_driver()
	var dir := Directory.new()
	dir.make_dir_recursive(OUT_DIR)

	var root := Spatial.new()
	get_root().add_child(root)

	var cam := Camera.new()
	cam.translation = Vector3(0, 0, 5)
	cam.current = true
	cam.fov = 50.0
	root.add_child(cam)

	env = Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.02, 0.02, 0.03)
	env.glow_enabled = true
	for i in range(4):
		env.set_glow_level(i, true)
	env.glow_intensity = 1.5
	env.glow_strength = 1.0
	env.glow_bloom = 0.0
	env.glow_hdr_threshold = 0.5
	env.glow_blend_mode = Environment.GLOW_BLEND_MODE_SCREEN
	cam.environment = env

	var quad := MeshInstance.new()
	var qm := QuadMesh.new()
	qm.size = Vector2(0.4, 0.4)
	quad.mesh = qm
	var mat := SpatialMaterial.new()
	mat.albedo_color = Color(0, 0, 0)
	mat.emission_enabled = true
	mat.emission = Color(1, 1, 1)
	mat.emission_energy = 4.0
	quad.material_override = mat
	root.add_child(quad)

	# Mapa completamente negro: con strength 1 apaga el glow en toda la pantalla.
	var img := Image.new()
	img.create(4, 4, false, Image.FORMAT_RGB8)
	img.fill(Color(0, 0, 0))
	var map := ImageTexture.new()
	map.create_from_image(img, 0)
	env.glow_map = map
	env.glow_map_strength = 1.0

	connect("idle_frame", self, "_on_frame")

func _on_frame():
	frames += 1
	if frames == MAPPED_FRAMES:
		mapped_img = _grab("mapped")
	elif frames == MAPPED_FRAMES + 1:
		env.glow_map = null
	elif frames == CLEAR_FRAMES:
		clear_img = _grab("clear")
		var ok := _report()
		quit(0 if ok else 1)

func _grab(tag: String) -> Image:
	var img = get_root().get_texture().get_data()
	img.flip_y()
	img.save_png(OUT_DIR.plus_file("glow_" + tag + ".png"))
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
	var cy = s.y / 2
	# Halo justo fuera de la silueta del emisor (quad de 0.4 unidades ~30 px).
	var halo_mapped = _mean_luma(mapped_img, cx + 40, cy - 25, cx + 80, cy + 25)
	var halo_clear = _mean_luma(clear_img, cx + 40, cy - 25, cx + 80, cy + 25)
	var ctrl_mapped = _mean_luma(mapped_img, 20, 20, 60, 60)
	var ctrl_clear = _mean_luma(clear_img, 20, 20, 60, 60)
	var delta = halo_clear - halo_mapped
	var control = abs(ctrl_clear - ctrl_mapped)
	print("GLOW_TEST driver=", driver, " size=", s, " halo_mapped=", halo_mapped, " halo_clear=", halo_clear, " delta=", delta, " control_delta=", control)
	if driver == OS.VIDEO_DRIVER_GLES2:
		if control < 0.02 and abs(delta) < 0.02:
			print("GLOW_GLES2_OK")
			return true
		print("GLOW_GLES2_FAIL")
		return false
	if delta > 0.05 and control < 0.02:
		print("GLOW_OK")
		return true
	print("GLOW_FAIL")
	return false