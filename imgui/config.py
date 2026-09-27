from SCons.Script import ARGUMENTS

# Opciones del modulo. En Godot 3 el SConstruct no recoge `get_opts` de los
# modulos, asi que `configure` lee los argumentos de la linea de comandos
# directamente (`imgui_implot=no`, etc.). `get_opts` se expone ademas con la
# firma esperada por si una version futura la usa.
#
# Perfil por defecto del fork: liviano. ImPlot entra (grafica los stats mas
# comunes en HUDs/tools), ImPlot3D no (poco uso fuera de demos y agrega
# thirdparty + CPPDEFINES). Un proyecto que lo necesite lo pide con
# `imgui_implot3d=yes` (asi hace gdtk para su panel de demos ImPlot3D).


def _opt(name, default):
    value = ARGUMENTS.get(name)
    if value is None:
        return default
    text = str(value).strip().lower()
    if text in ("no", "false", "0", "off", "n", ""):
        return False
    if text in ("yes", "true", "1", "on", "y"):
        return True
    if text == "auto":
        return "auto"
    return default


def get_opts(platform=None):
    return [
        ("imgui_implot", "Compilar y bindear ImPlot (yes/no)", "yes"),
        ("imgui_implot3d", "Compilar y bindear ImPlot3D (yes/no)", "no"),
        ("imgui_demos", "Compilar las demos (yes/no/auto)", "auto"),
    ]


def can_build(env, platform):
    # Node2D + VisualServer existen tambien en platform=server (headless):
    # el modulo compila en todas las plataformas del release.
    return True


def configure(env):
    demos = _opt("imgui_demos", "auto")
    if demos == "auto":
        demos = bool(env["tools"])

    # Solo se guardan las decisiones; los CPPDEFINES se aplican al entorno del
    # modulo en SCsub para no forzar la recompilacion de todo el motor.
    env["imgui_implot"] = bool(_opt("imgui_implot", True))
    env["imgui_implot3d"] = bool(_opt("imgui_implot3d", False))
    env["imgui_demos"] = bool(demos)
