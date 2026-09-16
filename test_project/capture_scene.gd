extends SceneTree

# Captura un frame de una escena para el harness de comparación de glitches.
# Variables de entorno:
#   CAPTURE_SCENE  res:// ruta de la escena a instanciar
#   CAPTURE_OUT    ruta del PNG de salida
#   CAPTURE_FRAMES frames a esperar antes de capturar (default 120)

var out_path: String
var frames: int = 120
var counted: int = 0

func _init():
	out_path = OS.get_environment("CAPTURE_OUT")
	frames = int(OS.get_environment("CAPTURE_FRAMES"))
	var scene_path = OS.get_environment("CAPTURE_SCENE")
	var packed = load(scene_path)
	if packed == null:
		push_error("no se pudo cargar " + scene_path)
		quit(1)
		return
	var scene = packed.instance()
	get_root().add_child(scene)
	if OS.get_environment("CAPTURE_PAUSE") == "1":
		paused = true
	connect("idle_frame", self, "_on_frame")

func _on_frame():
	counted += 1
	if counted < frames:
		return
	var img = get_root().get_texture().get_data()
	img.flip_y()
	var err = img.save_png(out_path)
	print("CAPTURED ", out_path, " err=", err, " frames=", counted)
	quit()
