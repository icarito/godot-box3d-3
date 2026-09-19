extends SceneTree

# Captura la secuencia de frames del demo de glow map, para armar el preview
# (preview.gif) con ffmpeg. Corre con contexto GL real:
#
#   GLOW_DEMO_OUT=/ruta/salida xvfb-run -a -s "-screen 0 1024x600x24" \
#     godot.x11.opt.tools.64 --path demos/glow_map --video-driver GLES3 \
#     -s preview.gd
#
# Barre glow_map_strength de 0 a 1 y vuelta a 0 sobre la escena animada, asi que
# el GIF muestra el patron de suciedad apareciendo y desapareciendo. Tambien deja
# dos stills: map_off.png (strength 0) y map_on.png (strength 1).

const WARMUP := 90

var out_dir := ""
var frames_target := 72
var counted := 0
var demo
var stage := "sweep"
var wait := 0


func _init():
	out_dir = OS.get_environment("GLOW_DEMO_OUT")
	if out_dir == "":
		out_dir = OS.get_user_data_dir() + "/glow_map_preview"
	var frames_env := OS.get_environment("GLOW_DEMO_FRAMES")
	if frames_env != "":
		frames_target = int(frames_env)

	var dir := Directory.new()
	dir.make_dir_recursive(out_dir)
	print("GLOW_DEMO_OUT ", out_dir)

	get_root().set_size(Vector2(640, 360))

	var packed = load("res://demo.tscn")
	if packed == null:
		push_error("no pude cargar res://demo.tscn")
		quit(1)
		return
	demo = packed.instance()
	get_root().add_child(demo)

	connect("idle_frame", self, "_on_frame")


func _on_frame():
	counted += 1
	if counted <= WARMUP:
		return

	var f := counted - WARMUP - 1
	match stage:
		"sweep":
			if f <= frames_target:
				# 0 -> 1 -> 0 a lo largo del barrido.
				var t := float(f) / float(frames_target)
				demo.set_preview_strength(sin(t * PI))
				_save_frame(f)
			else:
				demo.set_preview_strength(0.0)
				stage = "still_off"
				wait = 3
		"still_off":
			# El texture del viewport tiene el frame anterior: deja pasar unos
			# frames tras cambiar la fuerza antes de capturar.
			wait -= 1
			if wait <= 0:
				_save_still("map_off")
				demo.set_preview_strength(1.0)
				stage = "still_on"
				wait = 3
		"still_on":
			wait -= 1
			if wait <= 0:
				_save_still("map_on")
				print("GLOW_DEMO_CAPTURE_DONE frames=", frames_target)
				quit(0)


func _save_frame(index: int):
	var img = get_root().get_texture().get_data()
	img.flip_y()
	img.save_png(out_dir.plus_file("frame_%04d.png" % index))


func _save_still(tag: String):
	var img = get_root().get_texture().get_data()
	img.flip_y()
	img.save_png(out_dir.plus_file(tag + ".png"))