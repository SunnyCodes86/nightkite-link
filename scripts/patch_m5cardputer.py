from pathlib import Path

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
pioenv = env.subst("$PIOENV")
target = (
    project_dir
    / ".pio"
    / "libdeps"
    / pioenv
    / "M5Cardputer"
    / "src"
    / "utility"
    / "Keyboard"
    / "KeyboardReader"
    / "IOMatrix.cpp"
)

if target.exists():
    text = target.read_text()
    include = "#include <driver/gpio.h>\n"
    if include not in text:
        marker = "#include \"IOMatrix.h\"\n"
        text = text.replace(marker, marker + include, 1)
        target.write_text(text)
