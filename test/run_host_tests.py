#!/usr/bin/env python3
"""All portable assert tests, C++11 and warnings-as-errors; optional sanitizers."""
import argparse
import subprocess
import tempfile
import re
from pathlib import Path
root = Path(__file__).resolve().parents[1]
args = argparse.ArgumentParser()
args.add_argument('--sanitize', action='store_true')
options = args.parse_args()
sources = {
 'audio_sync': ['AudioSyncDsp'], 'battery_parser': ['ControllerBatteryParser'],
 'profile_compat': ['ProfileCodec'], 'sync_beacon': ['SyncBeaconCodec'],
 'uf2_validation': ['Uf2ValidationCore'],
 'show_control': ['ShowControl', 'ShowInput', 'SyncBeaconCodec'],
 'show_runtime': ['ShowControl', 'ShowInput', 'SyncBeaconCodec', 'ShowRuntime'],
}
tests = sorted((root/'test').glob('*/test_*.cpp'))
with tempfile.TemporaryDirectory(prefix='nightkite-link-tests-') as temp:
 for test in tests:
  exe = str(Path(temp)/test.stem)
  command = ['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pthread',
   '-I'+str(root/'include'), '-I'+str(root/'src/targets/tab5'),
   '-I'+str(root/'.pio/libdeps/cardputer/ArduinoJson/src')]
  if test.parent.name == 'cardputer_canvas':
   source = (root/'src/main.cpp').read_text()
   binding = re.search(r'^M5Canvas uiCanvas\(.*\);$', source, re.M)
   assert binding, 'Cardputer canvas binding'
   (Path(temp)/'canvas_under_test.inc').write_text(binding.group())
   command += ['-I'+temp]
  if test.parent.name == 'cardputer_usb':
   full_source = (root/'src/main.cpp').read_text()
   role = re.search(r'void beginUsbRole\(\)\n\{.*?\n\}', full_source, re.S)
   assert role, 'USB role boot initialization'
   (Path(temp)/'usb_role_under_test.inc').write_text(role.group())
   source = full_source.split('class UsbHostSerialTransport', 1)[1]
   methods = []
   for signature in ('void begin()', 'bool connected() override'):
    match = re.search(re.escape(signature) + r'\n  \{.*?\n  \}', source, re.S)
    assert match, signature
    methods.append(match.group().replace(' override', ''))
   (Path(temp)/'usb_transport_under_test.inc').write_text('\n'.join(methods))
   command += ['-I'+temp]
  if test.parent.name == 'show_runtime': command += ['-I'+str(test.parent/'stubs')]
  if options.sanitize:
   command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
  command += [str(root/'src'/(s+'.cpp')) for s in sources.get(test.parent.name, [])]
  command += [str(test), '-o', exe]
  subprocess.run(command, check=True)
  run = [exe]
  if test.parent.name == 'profile_compat': run += [str(test.with_name('legacy_profile_v2.json'))]
  subprocess.run(run, check=True)
  print('PASS', test.relative_to(root), flush=True)
print(f'{len(tests)}/{len(tests)} PASS' + (' (ASan/UBSan)' if options.sanitize else ''))
