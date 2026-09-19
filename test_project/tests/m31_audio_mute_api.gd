extends Spatial

# M31 acceptance: AudioServer mute / runtime driver swap (backport PR #63458).
#
# AudioServer.set_enabled(false) mutes audio and swaps the active driver to the
# Dummy driver; is_enabled() reflects it. The API must be present and safe on
# every path (dummy fallback included), and the four project settings must be
# registered. Toggles a few times and quits.

const MAX_FRAMES = 90

var frames = 0
var problems := 0


func _check(cond: bool, what: String) -> void:
	if not cond:
		problems += 1
		print("CHECK FAILED: ", what)


func _ready():
	print("audio_drivers=", OS.get_audio_driver_count(), " enabled=", AudioServer.is_enabled())

	_check(AudioServer.has_method("set_enabled"), "AudioServer.set_enabled bound")
	_check(AudioServer.has_method("is_enabled"), "AudioServer.is_enabled bound")
	_check(AudioServer.is_enabled(), "audio enabled by default")

	_check(ProjectSettings.has_setting("audio/muting/mute_driver"), "audio/muting/mute_driver registered")
	_check(ProjectSettings.has_setting("audio/muting/mute_on_pause"), "audio/muting/mute_on_pause registered")
	_check(ProjectSettings.has_setting("audio/muting/mute_on_silence"), "audio/muting/mute_on_silence registered")
	_check(ProjectSettings.has_setting("audio/muting/mute_on_focus_loss"), "audio/muting/mute_on_focus_loss registered")


func _process(_delta):
	frames += 1

	if frames == 20:
		AudioServer.set_enabled(false)
		_check(not AudioServer.is_enabled(), "set_enabled(false) mutes")
	elif frames == 30:
		AudioServer.set_enabled(false)
		_check(not AudioServer.is_enabled(), "disable is idempotent")
	elif frames == 40:
		AudioServer.set_enabled(true)
		_check(AudioServer.is_enabled(), "set_enabled(true) unmutes")
	elif frames == 50:
		AudioServer.set_enabled(false)
		AudioServer.set_enabled(true)
		_check(AudioServer.is_enabled(), "back-to-back toggle ends enabled")

	if frames < MAX_FRAMES:
		return

	print("RESULT problems=%d -> %s" % [problems, "PASS" if problems == 0 else "FAIL"])
	get_tree().quit(0 if problems == 0 else 1)