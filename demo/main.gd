extends CanvasLayer

@onready var violin = $Violin
@onready var drums = $Drums

func change_player():
	if violin.playing:
		violin.playing = false
		drums.play()
	elif drums.playing:
		drums.playing = false
	else:
		violin.play()


func toggle_effect():
	var enable = !AudioServer.is_bus_effect_enabled(0, 0)
	AudioServer.set_bus_effect_enabled(0, 0, enable)
