extends SceneTree
# Benchmark headless del renderer Slug (fase 1). Mide ms/frame de pared con
# varias cargas y compara contra un Label3D rasterizado (DynamicFont).
#
#   godot --no-window --path demos/slug_vector -s bench.gd
#
# Bajo llvmpipe (software) el número no es el de una GPU real, pero sirve para
# comparar cargas y ver el escalado de Slug vs rasterizado.

const CFF_PATH := "res://fonts/SourceSans3-Regular.otf"
const TTF_PATH := "res://fonts/NotoSans-Regular.ttf"
const TEXT := "Hamburgefonstiv 0123"
const WARMUP := 15
const FRAMES := 60

var world: Spatial


func _init():
	var cff_data := DynamicFontData.new()
	cff_data.font_path = CFF_PATH
	var ttf_data := DynamicFontData.new()
	ttf_data.font_path = TTF_PATH

	var cff := SlugFont.new()
	cff.font_data = cff_data
	var ttf := SlugFont.new()
	ttf.font_data = ttf_data

	var svg := SlugVector.new()
	svg.svg_path = "res://assets/hud.svg"

	if not cff.is_valid() or not ttf.is_valid():
		print("BENCH_FAIL faltan fuentes (corré demos/slug_vector/fetch_fonts.sh)")
		quit(1)
		return
	if not svg.is_valid():
		print("BENCH_FAIL falta assets/hud.svg")
		quit(1)
		return

	print("Slug CFF: %d glifos, %.2f ms build, %d curvas/banda máx" % [
		cff.get_glyph_count(), cff.get_build_time_usec() / 1000.0, cff.get_max_curves_per_band()])
	print("Slug TTF: %d glifos, %.2f ms build, %d curvas/banda máx" % [
		ttf.get_glyph_count(), ttf.get_build_time_usec() / 1000.0, ttf.get_max_curves_per_band()])
	print("Slug SVG: %d formas, %.2f ms build, %d curvas/banda máx" % [
		svg.get_shape_count(), svg.get_build_time_usec() / 1000.0, svg.get_max_curves_per_band()])
	print("")
	print("%-34s %10s %12s" % ["carga", "ms/frame", "equivalente"])

	world = Spatial.new()
	get_root().add_child(world)
	_add_environment()
	yield(self, "idle_frame")
	var camera := Camera.new()
	camera.translation = Vector3(0, 0, 14) # mira hacia -Z por defecto: al origen
	world.add_child(camera)

	# calentamiento de shaders/contexto
	_spawn(1, 1.0, cff, null, null)
	for i in range(WARMUP):
		yield(self, "idle_frame")
	_clear()

	yield(_bench("Slug CFF 1 etiqueta 1em", 1, 1.0, cff, null, null), "completed")
	yield(_bench("Slug CFF 10 etiquetas 1em", 10, 1.0, cff, null, null), "completed")
	yield(_bench("Slug CFF 40 etiquetas 1em", 40, 1.0, cff, null, null), "completed")
	yield(_bench("Slug CFF 1 etiqueta 6em", 1, 6.0, cff, null, null), "completed")
	yield(_bench("Slug CFF 8 etiquetas 6em", 8, 6.0, cff, null, null), "completed")
	yield(_bench("Slug TTF 8 etiquetas 6em", 8, 6.0, ttf, null, null), "completed")
	yield(_bench("Label3D 8 etiquetas 6em", 8, 6.0, null, ttf_data, null), "completed")
	yield(_bench("Slug SVG 1 icono 2u", 1, 2.0, null, null, svg), "completed")
	yield(_bench("Slug SVG 16 iconos 2u", 16, 2.0, null, null, svg), "completed")
	yield(_bench("Slug SVG 16 iconos 6u", 16, 6.0, null, null, svg), "completed")

	print("")
	print("BENCH_OK")
	quit(0)


func _add_environment():
	var we := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.02, 0.02, 0.03)
	we.environment = env
	world.add_child(we)


func _spawn(count: int, size: float, slug_font, dyn_data, svg):
	var cols := 4
	for i in range(count):
		var col := i % cols
		var row := int(i / cols)
		var pos := Vector3((col - (cols - 1) * 0.5) * size * 3.0, -(row * size * 0.4), 0)
		if svg != null:
			var icon := SlugVector3D.new()
			icon.vector = svg
			icon.size = size
			icon.translation = pos
			world.add_child(icon)
		elif slug_font != null:
			var label := SlugLabel3D.new()
			label.text = TEXT
			label.font = slug_font
			label.size = size
			label.align = SlugLabel3D.ALIGN_CENTER
			label.translation = pos
			world.add_child(label)
		else:
			var label := Label3D.new()
			label.text = TEXT
			var dyn := DynamicFont.new()
			dyn.font_data = dyn_data
			dyn.size = 96
			label.font = dyn
			label.pixel_size = size / 96.0
			label.translation = pos
			world.add_child(label)


func _clear():
	for child in world.get_children():
		if child is SlugLabel3D or child is Label3D or child is SlugVector3D:
			world.remove_child(child)
			child.queue_free()
	yield(self, "idle_frame")


func _bench(name: String, count: int, size: float, slug_font, dyn_data, svg):
	_spawn(count, size, slug_font, dyn_data, svg)
	for i in range(WARMUP):
		yield(self, "idle_frame")
	var t0 := OS.get_ticks_usec()
	for i in range(FRAMES):
		yield(self, "idle_frame")
	var ms := (OS.get_ticks_usec() - t0) / 1000.0 / FRAMES
	print("%-34s %9.2f ms %9.1f fps" % [name, ms, 1000.0 / max(ms, 0.001)])
	yield(_clear(), "completed")
