#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory() as directory:
    executable = Path(directory) / "nk4_line_parser_test"
    subprocess.run(
        [
            "c++",
            "-std=c++11",
            "-Wall",
            "-Wextra",
            f"-I{root / 'include'}",
            str(Path(__file__).with_name("test_nk4_line_parser.cpp")),
            "-o",
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
