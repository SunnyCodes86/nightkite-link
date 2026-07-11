#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory() as directory:
    executable = Path(directory) / "uf2_validation_test"
    subprocess.run(
        [
            "c++",
            "-std=c++11",
            "-Wall",
            "-Wextra",
            f"-I{root / 'include'}",
            str(root / "src/Uf2ValidationCore.cpp"),
            str(Path(__file__).with_name("test_uf2_validation.cpp")),
            "-o",
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
