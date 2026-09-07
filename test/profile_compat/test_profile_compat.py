#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
arduino_json = root / ".pio/libdeps/cardputer/ArduinoJson/src"
assert arduino_json.is_dir(), "run `python3 scripts/pio_target.py cardputer` first"

with tempfile.TemporaryDirectory() as directory:
    executable = Path(directory) / "profile_codec_test"
    subprocess.run(
        [
            "c++",
            "-std=c++11",
            f"-I{root / 'include'}",
            f"-I{arduino_json}",
            str(root / "src/ProfileCodec.cpp"),
            str(Path(__file__).with_name("test_profile_codec.cpp")),
            "-o",
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable), str(Path(__file__).with_name("legacy_profile_v2.json"))], check=True)
