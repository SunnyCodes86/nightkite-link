#!/usr/bin/env python3
"""Run one target with an isolated PlatformIO package directory."""
import os
import subprocess
import sys
from pathlib import Path


root = Path(__file__).resolve().parents[1]
targets = ("cardputer", "tab5")
selected = sys.argv[1] if len(sys.argv) > 1 else "all"
extra = sys.argv[2:]
if selected not in targets + ("all",):
    raise SystemExit("usage: pio_target.py [all|cardputer|tab5] [pio run arguments]")
if selected == "all" and extra:
    raise SystemExit("extra PlatformIO arguments require one explicit target")

for target in targets if selected == "all" else (selected,):
    env = os.environ.copy()
    env["PLATFORMIO_CORE_DIR"] = str(root / ".pio" / ("core-" + target))
    try:
        result = subprocess.run(["pio", "run", "-e", target] + extra, cwd=root, env=env)
    except KeyboardInterrupt:
        raise SystemExit(130)
    except OSError as error:
        raise SystemExit(f"pio: {error}")
    if result.returncode:
        raise SystemExit(result.returncode)
