# NightKite Link - Agent Instructions

## Scope

NightKite Link is the Cardputer-Adv handheld configurator and service tool for NightKite Multi controllers.

- Work only in this repository unless the user explicitly asks otherwise. Do not modify `nightkite-multi` or `nightkite-configurator` from a Link task.
- Preserve unrelated user changes. Do not commit, push, upload firmware or operate hardware unless explicitly requested.
- Treat current source and `platformio.ini` as authoritative for implementation details. Keep EN/DE documentation synchronized with user-visible behavior.

## Durable Product Invariants

- Preserve the current card-carousel UI and its small-screen usability. Do not restore the reverted task-based UI or introduce another large redesign.
- USB remains the stable primary transport: probe NK4 for Firmware 4.x and retain the Firmware 3.x Legacy CLI fallback.
- BLE NK4 remains an experimental, explicitly selected, single-controller configuration transport.
- Link is not required for autonomous controller sync. Do not use GATT/NK4 for real-time sync, relay sync beacons, stream LED frames or add Wi-Fi.
- Preserve profile v2 and NightKite Configurator compatibility.
- Preserve RP2040 and RP2350 UF2 flashing paths and all target-safety checks.
- Preserve hardware-proven UI, transport, audio and timing behavior unless a concrete defect justifies change.

## Repository Boundaries

- Reuse existing helpers in `include/` and `src/`; keep non-trivial logic host-testable where practical.
- Treat `lib/usb_host_msc/` as vendored code and `scripts/patch_m5cardputer.py` as required build support. Do not replace, upgrade or remove either casually.
- Do not upgrade platforms, libraries or add dependencies unless requested.
- Avoid broad rewrites, speculative abstractions, formatting-only changes and unrelated cleanup.

## Protocol, Queue And Session Safety

- NK4 must preserve sequence matching and handle success, error and event responses without depending on field order. Unknown or missing optional fields and capability-based `unsupported` responses must remain compatible.
- Preserve Legacy behavior and its intentional 22-pattern limit; NK4 supports the controller-reported catalog up to Link's 27 patterns.
- A controller session is usable only after protocol detection and its required initial refresh complete.
- On disconnect, reconnect or transport change, clear pending work and controller-derived state. Commands, responses and BLE notifications from an old generation must never affect a new controller/session.
- Preserve command order. Queue overflow must be reported; background work may yield before user commands. Live-command coalescing must not cross user-command barriers.
- A multi-command operation succeeds only after every confirmable command succeeds. Timeout, enqueue failure, partial failure or disconnect must end it as failed and leave the UI retryable.
- Keep polling and timeouts non-blocking and use wrap-safe `millis()` arithmetic.

## UI And Persistence Safety

- Keep local draft, live-applied and persistently-saved state distinct. Automatic refresh must not overwrite active drafts.
- Do not report persistence before a confirmed save. Failed commands, partial batches, failed saves or edits made after save began must remain visibly unsaved.
- Profile apply changes live settings and must not silently save them.
- Keep controller and Link battery/status information distinct and clear stale controller values after session loss.
- Keep keyboard handling, footer hints, queue status and navigation usable on the physical 240 x 135 px device. Critical flash states may lock navigation; ordinary work must not block the main loop.

## BLE And Audio Safety

- Keep BLE scanning and connection attempts bounded and keep callback/main-loop state synchronized.
- Preserve callback/client lifetime safety, notification generation checks and bounded newline-based NK4 reassembly.
- Recover intentional and unexpected disconnects to a clean retryable state. Do not add automatic reconnect that could select the wrong controller.
- Audio Beacon advertising remains separate from BLE GATT. Preserve supported V1/V2 and microphone modes, established DSP behavior and speaker/microphone arbitration.
- Change DSP or beacon payload behavior only for a demonstrated issue and add a focused host test.

## Profiles And SD Safety

- Validate profile size, schema, types and ranges before replacing loaded state. Reject malformed, truncated or unsupported input safely; tolerate unknown fields and apply compatible defaults for missing optional fields.
- Preserve compact pattern-mask and legacy `patterns[]` compatibility, custom names and controller capability clamping.
- Keep profile writes recoverable: write and verify temporary data before replacement, retain backup/restore behavior and surface every SD I/O failure.
- Apply profiles only to the fully initialized session the user confirmed; cancel on disconnect or session change.

## UF2 Safety

Treat firmware flashing as safety-critical.

- Validate the complete UF2 structure and family against the selected RP2040/RP2350 target before flashing.
- Match the connected BOOTSEL device to the selected target before the first write; filenames and UI labels are not device evidence.
- Suspend normal controller traffic while flashing and check all reads, writes, flushes, closes and unmounts.
- Partial writes, early disconnects, cancellation and timeouts are failures and must clean up safely.
- Report success only after complete copy and the expected BOOTSEL disconnect/reboot. Never convert a reboot timeout into success.
- Target matching, accepted devices, write paths and success criteria require focused tests and validation on both hardware families.

## Validation And Handoff

- For non-trivial logic changes, add or update the smallest meaningful host test and run all relevant existing tests under `test/`.
- For source changes, run the default PlatformIO build. Run any additional environment explicitly required by the task.
- Always run `git diff --check` and inspect the final diff for unrelated or generated changes.
- Update both EN and DE docs for user-visible behavior changes.
- Hardware-test changes affecting USB/Legacy, BLE lifecycle, physical UI, SD failures, audio arbitration/beacons or UF2. State clearly what was not tested on hardware.
- Before changing shared queue, parser, transport, session, persistence or flashing logic, trace its callers and fix the root cause at the common boundary.
