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

	# Icono estilo Sugar: entidades fill_color/stroke_color + strokes reales.
	var sugar := SlugVector.new()
	sugar.svg_path = "res://assets/sugar_icon.svg"
	if not sugar.is_valid():
		print("SLUG_FAIL: SlugVector inválido para sugar_icon.svg")
		quit(1)
		return
	if sugar.get_shape_count() < 6:
		print("SLUG_FAIL: sugar_icon %d formas (se esperaban >= 6: fill+stroke)" % sugar.get_shape_count())
		quit(1)
		return
	# Recolorear en runtime no debe reconstruir ni fallar.
	sugar.fill_color = Color(1, 0, 0)
	sugar.stroke_color = Color(0, 1, 0)
	var sicon := SlugVector3D.new()
	sicon.vector = sugar
	sicon.size = 2.0
	sicon.translation = Vector3(3, 0, 0)
	get_root().add_child(sicon)
	print("Sugar SVG: %d formas, %.2f ms build, %d curvas/banda máx" % [
		sugar.get_shape_count(), sugar.get_build_time_usec() / 1000.0, sugar.get_max_curves_per_band()])

	# Gradientes (lineal objectBoundingBox + radial userSpaceOnUse) y even-odd.
	var grad := SlugVector.new()
	grad.svg_path = "res://assets/gradient_icon.svg"
	if not grad.is_valid():
		print("SLUG_FAIL: SlugVector inválido para gradient_icon.svg")
		quit(1)
		return
	if grad.get_shape_count() < 5:
		print("SLUG_FAIL: gradient_icon %d formas (se esperaban >= 5)" % grad.get_shape_count())
		quit(1)
		return
	grad.fill_color = Color(0.2, 0.6, 1.0)
	grad.stroke_color = Color(0.05, 0.1, 0.2)
	var gicon := SlugVector3D.new()
	gicon.vector = grad
	gicon.size = 2.5
	gicon.translation = Vector3(-3, 0, 0)
	get_root().add_child(gicon)
	print("Gradiente SVG: %d formas, %.2f ms build, %d curvas/banda máx" % [
		grad.get_shape_count(), grad.get_build_time_usec() / 1000.0, grad.get_max_curves_per_band()])

	# Nodo 2D para UI plana: mismo SlugVector, material canvas_item.
	var ui := SlugVector2D.new()
	ui.vector = sugar
	ui.size = 128
	ui.centered = true
	get_root().add_child(ui)
	print("SlugVector2D: ok")

	yield(self, "idle_frame")
	yield(self, "idle_frame")
	print("SLUG_OK")
	quit(0)
