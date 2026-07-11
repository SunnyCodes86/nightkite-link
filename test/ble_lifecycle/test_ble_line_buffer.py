#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory() as directory:
    executable = Path(directory) / "ble_line_buffer_test"
    subprocess.run(
        [
            "c++",
            "-std=c++11",
            "-Wall",
            "-Wextra",
            "-pthread",
            f"-I{root / 'include'}",
            str(Path(__file__).with_name("test_ble_line_buffer.cpp")),
            "-o",
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
