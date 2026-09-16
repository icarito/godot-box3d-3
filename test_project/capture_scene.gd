extends SceneTree

# Captura un frame de una escena para el harness de comparación de glitches.
# Variables de entorno:
#   CAPTURE_SCENE  res:// ruta de la escena a instanciar
#   CAPTURE_OUT    ruta del PNG de salida
#   CAPTURE_FRAMES frames a esperar antes de capturar (default 120)
#   CAPTURE_VIEWPORT ruta (find_node desde la raíz) del Viewport a capturar.
#                    Sin esto captura el viewport raíz; el menú de Odisea dibuja
#                    el Pilot en Backdrop/Preview3D/Viewport, que el raíz no ve.

var out_path: String
var frames: int = 120
var counted: int = 0
var capture_viewport: Viewport = null

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
	var viewport_path = OS.get_environment("CAPTURE_VIEWPORT")
	if viewport_path != "":
		capture_viewport = scene.get_node_or_null(viewport_path)
		if capture_viewport == null:
			push_error("no se encontró el viewport " + viewport_path + " (relativo a la raíz de la escena)")
			quit(1)
			return
		print("CAPTURE_VIEWPORT ", viewport_path, " size=", capture_viewport.get_size())
	var pose_seconds = OS.get_environment("CAPTURE_ANIMATION_TIME")
	if pose_seconds != "":
		# Rutas explícitas si el proyecto las necesita; si no, se buscan el
		# AnimationPlayer y el Skeleton en la escena (en el menú de Odisea
		# cuelgan dentro de Backdrop/Preview3D/Viewport/Root3D/...).
		var anim_node = OS.get_environment("CAPTURE_ANIMATION_NODE")
		var animation = scene.get_node_or_null(anim_node) if anim_node != "" else _find_first(scene, "AnimationPlayer")
		if animation:
			var anim_name = OS.get_environment("CAPTURE_ANIMATION_NAME")
			if anim_name != "":
				animation.play(anim_name)
			elif animation.current_animation == "":
				animation.play(animation.get_animation_list()[0])
			animation.seek(float(pose_seconds), true)
			animation.stop(false)
			var skel_node = OS.get_environment("CAPTURE_SKELETON_NODE")
			var skeleton = scene.get_node_or_null(skel_node) if skel_node != "" else _find_first(scene, "Skeleton")
			if skeleton:
				var snapshot = ""
				for bone in skeleton.get_bone_count():
					snapshot += str(skeleton.get_bone_pose(bone))
				print("CAPTURE_SKELETON_MD5 ", snapshot.md5_text())
		else:
			push_error("CAPTURE_ANIMATION_TIME pero no encontré AnimationPlayer")
	if OS.get_environment("CAPTURE_PAUSE") == "1":
		paused = true
	connect("idle_frame", self, "_on_frame")

func _find_first(node: Node, cls: String) -> Node:
	if node.is_class(cls):
		return node
	for child in node.get_children():
		var found = _find_first(child, cls)
		if found != null:
			return found
	return null

func _on_frame():
	counted += 1
	if counted < frames:
		return
	var target: Viewport = capture_viewport if capture_viewport != null else get_root()
	var img = target.get_texture().get_data()
	img.flip_y()
	var err = img.save_png(out_path)
	print("CAPTURED ", out_path, " err=", err, " frames=", counted)
	quit()
