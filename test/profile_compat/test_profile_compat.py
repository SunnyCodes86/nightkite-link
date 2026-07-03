#!/usr/bin/env python3
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
profile = json.loads((Path(__file__).with_name("legacy_profile_v2.json")).read_text())
source = (root / "src" / "main.cpp").read_text()

assert profile["profile_version"] == 2
assert profile["settings"]["brightness"] == 159
assert profile["settings"]["autoplay"]["interval_seconds"] == 20
assert '"profile_version\\": 2' in source
assert 'jsonInt(json, "brightness"' in source
assert 'jsonBool(json, "enabled"' in source
assert 'jsonInt(json, "interval_seconds"' in source
