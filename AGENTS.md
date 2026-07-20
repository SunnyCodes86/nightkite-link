# NightKite Link - Agent Instructions

## Scope And Purpose

NightKite Link is the M5Stack Cardputer-Adv / StampS3 handheld configurator and service tool for NightKite Multi. It uses the 240 x 135 px display, keyboard, microSD and USB host; Firmware 4.x is controlled through NK4, Firmware 3.x through the Legacy CLI fallback.

- Work only in this repository unless the user explicitly asks otherwise. Do not modify `nightkite-multi` or `nightkite-configurator` from a Link task.
- Preserve unrelated user changes. Do not commit, push, upload firmware or operate hardware unless explicitly requested or required by the task.
- Current code is the source of truth. Keep `docs/README.en.md` and `docs/README.de.md` aligned with user-visible behavior.
- Keep time-sensitive sibling-project status and roadmaps out of this file.

## Product Baseline

- Preserve the current flat 17-card carousel UI. Do not restore the reverted task-based UI or propose another large redesign.
- USB is the stable primary transport. BLE NK4 is experimental, user-selected and limited to one active controller.
- Link configures and diagnoses controllers; it is not required for autonomous sync.
- BLE GATT/NK4 is not the real-time sync path. Do not relay sync beacons, stream LED frames or add Wi-Fi.
- Live changes, local drafts and persistent controller saves are separate states.
- The RP2040/RP2350 UF2 flasher is an experimental service/recovery workflow and is safety-critical.

## Repository Map

- `src/main.cpp`: application state, card UI, keyboard, USB/BLE transports, protocol parsing, command orchestration, profiles and workflows.
- `src/` and `include/`: host-testable DSP, microphone capture, battery, profile, sound, queue/session and UF2 components.
- `lib/usb_host_msc/`: vendored USB Mass Storage host code; do not replace or upgrade it casually.
- `scripts/patch_m5cardputer.py`: required build-time M5Cardputer compatibility patch.
- `test/`: native C++ checks, Python compiler wrappers and the legacy v2 compatibility fixture.

Reuse existing helpers and extend host-testable production logic where practical. Do not extract or abstract code solely for style.

## Build

PlatformIO has one default environment, `cardputer`:

- board `m5stack-stamps3`, Arduino framework, pioarduino ESP32 platform `51.03.03`
- `NIGHTKITE_USB_HOST=1`, monitor 115200 baud
- SD SPI pins: SCK 40, MISO 39, MOSI 14, CS 12

Do not upgrade the platform or libraries unless requested.

```sh
platformio run
platformio run -e cardputer
```

Use upload and serial-monitor commands only for requested hardware work. `.pio/` is generated output.

## UI Invariants

The card order is: `Status`, `Pattern Live`, `Brightness`, `Play`, `Audio Beacon`, `Patterns`, `Pattern Bulk`, `Profiles`, `Controller`, `BLE Connect`, `Controller Setup`, `Controller Sync`, `Controller Radio`, `Motion Service`, `Sync Diagnostics`, `Sync Setup Test`, `Firmware Update`.

- Keep the UI readable at 240 x 135 px, footer hints width-aware and the status bar compact.
- Preserve physical arrows plus `A/D`, `W/S` fallbacks and existing `Enter`, `Backspace`/`DEL`, `Tab`, `R`, `C`, `T`, `I` actions.
- Local drafts marked `*` must survive automatic refresh; discard them only through the existing cancel/session-reset paths.
- Preserve the queue indicator (`Q0`...`Q9+`, `Q!`), Link/controller battery distinction and pattern `UNSAVED` marker.
- UI, keyboard, audio and transport polling must remain responsive. Lock navigation only during critical UF2 states.

## Controller Sessions And Protocols

USB probing intentionally waits for the device, sends `protocol machine`, then probes `NK4 seq=<id> cmd=hello client=nightkite-link proto_min=4 proto_max=4`; a timeout falls back to Legacy CLI. `C reset USB` resets Link app/protocol/session state, not the controller or USB host stack.

- Preserve Legacy support and its intentional 22-pattern limit.
- A session is ready only after transport/controller connection, resolved protocol and successful `info`, `status`, `get section=config` and `get section=play`; Legacy `show` satisfies its initial refresh.
- Clear controller-derived state and pending work on disconnect or transport change. Never display values or send commands inherited from another controller.
- Connection generations protect against stale queued commands, NK4 responses and BLE notifications. Do not bypass them.
- NK4 parsing must match `seq`, accept `ok`, `err` and `event`, tolerate key order, unknown keys and absent optional fields, and remain non-blocking.
- Firmware 4.x behavior is capability-driven. Missing fields and `unsupported` responses can be valid compatibility cases.

## Command Queue

- Preserve command order and one-at-a-time NK4 sequence matching. Commands must never cross to a new transport/session.
- Capacity is 64 waiting entries. Deduplicate/drop optional initialization and polls first; report failure if a user command still cannot fit.
- Only unsent brightness or active-pattern commands of the same kind may coalesce, and never across another user-command barrier.
- Multi-command success requires every confirmable user command to succeed. Enqueue failure, timeout, partial failure or disconnect must end the operation cleanly as failed.
- Legacy batches may report only that they were sent because Legacy has no sequence-matched confirmation.
- Keep automatic polling conservative and paused during active drafts, transfer workflows and firmware flashing.

## BLE NK4 Lifecycle

GATT UUIDs are fixed: service `4e4b4000-6e69-6768-746b-000000000001`, RX `4e4b4000-6e69-6768-746b-000000000002`, TX `4e4b4000-6e69-6768-746b-000000000003`.

- Keep scan asynchronous/bounded and connect bounded; use the scanned address type.
- Callback objects and the reusable client have deliberate lifetimes. Do not allocate callbacks per attempt or delete a client while disconnect callbacks may reference it.
- Synchronize callback/main-loop state. Reassemble 20-byte notifications to newline, accept CRLF and bound line length/queue size.
- Keep notification generations, intentional/unexpected disconnect handling and recovery to a retryable state.
- Do not add automatic reconnect; explicit selection prevents reconnecting to the wrong controller.

## Patterns And Persistence

- Link supports IDs 1-27; 23-27 are audio patterns. NK4 uses the reported count capped at 27; Legacy stays capped at 22.
- Keep pattern IDs one-based, vector indices zero-based and mask operations limited to the connected controller's supported count.
- Preserve names, cycle/invert list/detail consistency and sync classifications `S`, `P`, `L`, `?`.
- Brightness/pattern edits apply live. Pattern state remains `UNSAVED` until confirmed `save`.
- Failed/timed-out commands, partial batches, failed save or a change after save began must leave persistence dirty.
- `S save` is explicit controller persistence. Profile apply must not silently save.

## Profiles And SD

- Preserve profile version 2, NightKite Configurator compatibility, custom names and the legacy fixture.
- JSON is limited to 8192 bytes and validated for version, type and range. Invalid input must not replace loaded state.
- Missing optional fields use current/default fallback; unknown fields are tolerated. Compact masks take precedence over legacy `patterns[]`.
- Keep temporary-write, verification and backup/restore replacement. Handle every SD/open/read/write/rename failure visibly.
- Apply only to a fully initialized current session; cancel on disconnect/session change and clamp patterns/masks to controller capability.

## Audio Beacon And Microphone

- Preserve `V1 Manual`, `V2 Manual`, `V2 Mic Energy` and `V2 Mic Full`; this non-connectable beacon mode is separate from GATT.
- Capture stays asynchronous at 8 kHz mono and 256 samples / 32 ms. Do not change proven Goertzel, gate, smoothing, sensitivity or beat/BPM/phase behavior without a demonstrated bug and host test.
- Suspend speaker sounds during microphone capture and restore clean BLE/audio state on exit.
- Preserve the V2 limit: 22-byte payload in a 29-byte legacy advertisement, with no local name or service data.

## UF2 Flasher Safety

- Validate the full file before flashing: existence, non-zero size, 512-byte alignment, 32 MiB limit, magic, payload bounds, block count/order/completeness, duplicates and target family ID.
- Before writing, require Raspberry Pi USB VID and the selected RP2040/RP2350 BOOTSEL PID. Filename and UI selection alone are not target evidence.
- Stop normal controller traffic while flashing. Check every read, write, flush, close and unmount result.
- Partial write, early disconnect, mount failure, timeout and cancellation are failures and must clean up safely.
- Report success only after complete copy and expected BOOTSEL disconnect/reboot; reboot timeout is failure.
- Changes to accepted devices, target validation, VFS/direct-sector paths or success criteria require focused tests and both-target hardware validation.

## Tests And Done Criteria

Run the focused host test for each non-trivial logic change. For queue/session/BLE/profile/DSP/flashing work, run the full host suite:

```sh
python3 test/ble_lifecycle/test_ble_line_buffer.py
python3 test/command_queue/test_command_queue.py
python3 test/controller_session/test_controller_session.py
python3 test/profile_compat/test_profile_compat.py
python3 test/uf2_validation/test_uf2_validation.py
c++ -std=c++11 -Wall -Wextra -Iinclude src/AudioSyncDsp.cpp test/audio_sync/test_audio_sync.cpp -o /tmp/nk_link_audio && /tmp/nk_link_audio
c++ -std=c++11 -Wall -Wextra -Iinclude src/ControllerBatteryParser.cpp test/battery_parser/test_battery_parser.cpp -o /tmp/nk_link_battery && /tmp/nk_link_battery
c++ -std=c++11 -Wall -Wextra -Iinclude test/ui_usability/test_ui_usability.cpp -o /tmp/nk_link_ui && /tmp/nk_link_ui
```

Build `cardputer` first if the profile test cannot find ArduinoJson in `.pio/libdeps/cardputer`.

Before handoff after source changes, run both builds, `git diff --check`, inspect the final diff and remove only generated test/build changes. Update both languages when behavior changes.

Hardware validation is required for USB enumeration/reconnect, Legacy, BLE lifecycle, Cardputer input/layout, SD failures, speaker/microphone arbitration, beacon reception and UF2 changes. Report explicitly what was not tested on hardware.

## Development Rules

- Trace every caller and fix root causes at shared queue/parser/transport/session/persistence/flashing boundaries.
- Prefer the smallest testable change; avoid unrelated refactors, speculative abstractions, new dependencies and formatting-only diffs.
- Preserve hardware-proven timings and behavior unless a concrete issue justifies change.
- Keep error paths explicit, state retryable after failure and `millis()` arithmetic wrap-safe.
- Do not remove sounds, Legacy support, profile compatibility or safety checks unless the task explicitly requires it.
