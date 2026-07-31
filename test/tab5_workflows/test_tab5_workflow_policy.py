#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory() as directory:
    executable = Path(directory) / "tab5_workflow_policy_test"
    subprocess.run(
        [
            "c++",
            "-std=c++11",
            "-Wall",
            "-Wextra",
            f"-I{root / 'src/targets/tab5'}",
            str(Path(__file__).with_name("test_tab5_workflow_policy.cpp")),
            "-o",
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
