extends SceneTree
# Chequeo headless: construye los dos SlugFont, arma un SlugLabel3D con todo
# el rango ASCII + Latin-1 y confirma que no explota.


func _init():
	var sans_data = DynamicFontData.new()
	sans_data.font_path = "res://fonts/NotoSans-Regular.ttf"
	var serif_data = DynamicFontData.new()
	serif_data.font_path = "res://fonts/NotoSerif-Regular.ttf"

	var sans_font = SlugFont.new()
	sans_font.font_data = sans_data
	var serif_font = SlugFont.new()
	serif_font.font_data = serif_data

	for entry in [["Sans", sans_font], ["Serif", serif_font]]:
		var name = entry[0]
		var font: SlugFont = entry[1]
		if not font.is_valid():
			_fail("%s no es válido (¿faltan las fuentes? corré fetch_fonts.sh)" % name)
			return
		if font.get_glyph_count() <= 180:
			_fail("%s tiene sólo %d glifos, se esperaban más de 180" % [name, font.get_glyph_count()])
			return
		print("%s: %d glifos, %.2f ms build, %d curvas/banda máx" % [
			name, font.get_glyph_count(),
			font.get_build_time_usec() / 1000.0,
			font.get_max_curves_per_band()
		])

	var text = ""
	for c in range(0x0020, 0x007E + 1):
		text += char(c)
	for c in range(0x00A0, 0x00FF + 1):
		text += char(c)

	var label = SlugLabel3D.new()
	label.text = text
	label.font = sans_font
	get_root().add_child(label)

	yield(self, "idle_frame")
	yield(self, "idle_frame")

	print("SLUG_OK")
	quit(0)


func _fail(reason: String):
	print("SLUG_FAIL %s" % reason)
	quit(1)
