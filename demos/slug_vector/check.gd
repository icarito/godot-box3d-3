extends SceneTree
# Chequeo headless del renderer vectorial: parsea el SVG del demo y arma un
# SlugVector3D.
#
#   godot --no-window --path demos/slug_vector -s check.gd
# imprime SLUG_OK y sale con 0.

func _init():
	var svg := SlugVector.new()
	svg.svg_path = "res://assets/hud.svg"

	if not svg.is_valid():
		print("SLUG_FAIL: SlugVector inválido (¿falta assets/hud.svg?)")
		quit(1)
		return

	var shapes := svg.get_shape_count()
	if shapes < 4:
		print("SLUG_FAIL: %d formas, se esperaban al menos 4" % shapes)
		quit(1)
		return

	var bounds: Rect2 = svg.get_bounds()
	if bounds.size.x <= 0.0 or bounds.size.y <= 0.0:
		print("SLUG_FAIL: bounds vacíos %s" % str(bounds))
		quit(1)
		return
	if svg.get_max_curves_per_band() <= 0:
		print("SLUG_FAIL: sin curvas")
		quit(1)
		return

	print("SVG: %d formas, %.2f ms build, %d curvas/banda máx, bounds %s" % [
		shapes, svg.get_build_time_usec() / 1000.0, svg.get_max_curves_per_band(), str(bounds)])

	var icon := SlugVector3D.new()
	icon.vector = svg
	icon.size = 2.0
	get_root().add_child(icon)

	yield(self, "idle_frame")
	yield(self, "idle_frame")
	print("SLUG_OK")
	quit(0)
