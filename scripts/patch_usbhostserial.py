from pathlib import Path

Import("env")

target = (
    Path(env.subst("$PROJECT_DIR"))
    / ".pio"
    / "libdeps"
    / env.subst("$PIOENV")
    / "USBHostSerial"
    / "src"
    / "USBHostSerial.cpp"
)

if target.exists():
    text = target.read_text()
    old = """if (xSemaphoreTake(thisInstance->_device_disconnected_sem, 0) == pdTRUE) {
        break;
      }"""
    new = """if (xSemaphoreTake(thisInstance->_device_disconnected_sem, 0) == pdTRUE) {
        xSemaphoreGive(thisInstance->_device_disconnected_sem);
        break;
      }"""
    if old in text:
        target.write_text(text.replace(old, new, 1))
    elif new not in text:
        raise RuntimeError("USBHostSerial disconnect guard changed upstream")
