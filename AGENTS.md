# NightKite Link - Agent Instructions

## Repository Scope

This repository contains NightKite Link, the handheld field configurator and service device for NightKite Multi.

Work only inside this repository unless the user explicitly asks otherwise. Do not modify `nightkite-multi` or `nightkite-configurator` from a NightKite Link task.

## Project Purpose

NightKite Link runs on M5Stack Cardputer-Adv / StampS3 hardware and is optimized for the 240 x 135 px Cardputer display. It is a field configurator, USB CLI/NK4 frontend, service/debug device, SD-card profile device, and optional UF2 service tool.

NightKite Link is not a real-time controller-to-controller sync relay. It configures controllers, applies profiles, and displays diagnostics. Real-time sync is handled by NightKite Multi firmware using compact sync beacons.

## Hardware And Build

Main target:
- M5Stack Cardputer-Adv / StampS3
- ESP32-S3
- Cardputer keyboard
- microSD card
- USB-OTG connection to NightKite Multi

PlatformIO environment:
- `cardputer`

Validation commands:
- `platformio run`
- `platformio run -e cardputer`
- `platformio run -t upload` only when upload is requested or useful
- `platformio device monitor` for manual serial checks

## Current NightKite Link State

Preserve existing behavior unless the user explicitly asks to change it:
- card-based UI and compact status bar
- USB legacy CLI fallback for Firmware 3.x
- USB NK4 support for Firmware 4.0+
- non-blocking command queue and NK4 seq matching
- Dirty/Draft state so auto-refresh does not overwrite active edits
- Cardputer/Link battery display
- controller battery display in percent and volts
- brightness, strip length, active pattern, smoothing, autoplay, autoplay interval
- pattern list, pattern detail views, cycle/invert state, bulk pattern actions
- SD profiles under `/profiles/`, including tolerant v2 fields
- UF2 firmware selection under `/firmware/`
- experimental USB Mass Storage UF2 flasher workflow
- startup/UI/navigation/success/error/transfer sounds
- Device Card `C reset USB` app/session reset for manual USB reconnect
- Sync Test Card with `Configure Master`, `Configure Follower`, `Save`, `Refresh Sync`
- Sync Test selections for Group 1-4 and `long_range` / `balanced` / `fast_sync`
- Sync Test actions `Name Master`, `Name Follower`, and `Play SYNC`

Do not replace the card UI with a large redesign. Keep screens readable on 240 x 135 px.

## Current NightKite Multi Firmware 4.0 State

Firmware 4.0-alpha is developed on the `dev` branch of `nightkite-multi`.

Relevant capabilities:
- Firmware 3.x legacy CLI remains available.
- USB NK4 works.
- Device identity exists: UID, short ID, device name, firmware version, protocol version, hardware ID.
- Play modes exist: `manual`, `autoplay`, `sync`.
- Sync settings exist: enabled, group, role, master UID, loss behavior.
- Wireless settings exist: enabled, profile, status.
- RM2/BLE works in `env:pico2350_rm2_ble`.
- NK4-over-BLE GATT works experimentally in firmware.
- SyncBeaconRadio works experimentally.
- Master/Follower beacon sync has been tested with two controllers.
- Followers receive and decode beacons.
- Followers follow pattern and brightness.
- Master autoplay in sync mode works.
- PatternClock diagnostics and pattern classification exist.
- Pattern classification includes `sync_ready_mask`, `partial_sync_mask`, and `local_reactive_mask`.

New sync / PatternClock diagnostics may include:
- `sync_autoplay`
- `master_autoplay`
- `autoplay_next_ms`
- `phase_ms`
- `beacon_phase_ms`
- `last_beacon_seq`
- `last_applied_seq`
- `sync_apply_count`
- `sync_apply_skipped`
- `sync_apply_reason`
- `last_pattern_change_latency_ms`
- `sync_ready_pattern`
- `partial_sync_pattern`

Firmware 4.0 is still alpha. Use capability detection and tolerate missing fields or `unsupported` responses.

## Connection Strategy

USB is the stable primary path.

On USB connect:
1. Wait briefly for the controller to settle.
2. Send `protocol machine` if needed.
3. Try NK4:
   `NK4 seq=<id> cmd=hello client=nightkite-link proto_min=4 proto_max=4`
4. If NK4 responds, use USB NK4 and load identity/status/config sections.
5. If NK4 does not respond, fall back to the existing USB legacy CLI workflow.

Never remove legacy support unless explicitly requested.

The manual Device Card `C reset USB` action should reset only app/protocol/session state and start fresh probing. Do not add risky USB host stack resets unless the user explicitly asks.

## BLE Direction

NightKite Multi firmware has experimental NK4-over-BLE GATT. NightKite Link does not yet have a BLE client.

Do not implement a BLE client unless the task explicitly asks for it. When BLE client work is requested, keep USB stable, reuse the NK4 protocol path, reassemble notify chunks until newline, and do not use BLE GATT/NK4 as the real-time sync mechanism.

Firmware BLE UUIDs:
- Service: `4e4b4000-6e69-6768-746b-000000000001`
- RX Write: `4e4b4000-6e69-6768-746b-000000000002`
- TX Notify: `4e4b4000-6e69-6768-746b-000000000003`

## NK4 Behavior

Support and preserve parsing for:
- `NK4 seq=<id> ok key=value ...`
- `NK4 seq=<id> err code=<error_code> msg=<short_message> ...`
- `NK4 event=<event_name> key=value ...`

Parser rules:
- preserve and match `seq`
- parse `key=value` in any order
- ignore unknown keys without crashing
- use safe defaults for missing fields
- keep timeouts non-blocking
- show useful errors for `unsupported`, `invalid_value`, `range_error`, `timeout`, `not_ready`, `busy`, `save_failed`, `sync_busy`, `sync_not_armed`, and `sync_too_late`

Important NK4 command groups:
- Core: `hello`, `info`, `caps`, `status`, `save`, `load`
- Sections: `get section=config|play|sync|wireless|patterns`
- Play: `set play_mode=manual|autoplay|sync`, `set boot_mode=last|manual|autoplay|sync`
- Config: `set name=...`, `set brightness=...`, `set pattern=...`, `set strip_length=...`, `set smoothing=...`, `set autoplay=...`, `set autoplay_interval=...`
- Patterns: `set enabled_mask=...`, `set inverted_mask=...`, plus legacy pattern commands where needed
- Sync: `set sync_enabled=0|1`, `set sync_group=...`, `set sync_role=standalone|master|follower`, `set sync_master_uid=...`, `set sync_loss_behavior=...`, `sync_status`
- Wireless: `set wireless_enabled=0|1`, `set wireless_profile=long_range|balanced|fast_sync`, `get section=wireless`, `ble_status` if supported
- Diagnostics: `battery`, `sensor`, `timing`, `offsets`

Prefer compact masks for Firmware 4.0 pattern state. Preserve Firmware 3.x legacy commands.

## UI Direction

Keep the existing card navigation and small-screen layout. Prefer concise labels and split dense diagnostics across cards rather than shrinking text until it is unreadable.

Status bar should stay compact, for example:
- `USB NK4 NK-Test SYNC/M 100% 4.25V | Link 91%`
- `USB LEG 100% | Link 91%`
- `NO USB`
- `USB DET`

Firmware 4.0 cards should expose:
- Device identity and connection mode
- Play mode and boot mode
- Sync settings and Sync Test setup
- Wireless settings
- Sync radio / beacon diagnostics
- PatternClock and pattern classification diagnostics when supported

For upcoming sync diagnostics work, prefer displaying the new Firmware 4.0 fields from `status`, `sync_status`, `get section=sync`, `get section=wireless`, and pattern sections. Do not assume every alpha build returns every field.

## Profiles

Profiles must remain backward-compatible. Missing fields should use defaults and never crash profile load/apply.

Supported or useful v2 fields include:
- `device_name`
- `play_mode`
- `boot_mode`
- `brightness`
- `strip_length`
- `smoothing`
- `active_pattern`
- `autoplay_enabled`
- `autoplay_interval`
- `enabled_pattern_mask`
- `inverted_pattern_mask`
- `sync_enabled`
- `sync_group`
- `sync_role`
- `sync_master_uid`
- `sync_loss_behavior`
- `wireless_enabled`
- `wireless_profile`

Do not destroy older profile compatibility without explicit instruction.

## Development Rules

- Keep USB host as the stable path.
- Keep Firmware 3.x legacy fallback working.
- Keep USB NK4 behind detection/handshake.
- Do not implement BLE client work unless requested.
- Do not use WiFi.
- Do not make NightKite Link mandatory for autonomous sync.
- Do not stream LED frames.
- Do not relay sync beacons through Link.
- Do not use NK4/GATT text commands as a real-time sync path.
- Keep Dirty/Draft state intact for editable fields.
- Keep auto-refresh conservative and non-blocking.
- Do not remove sounds unless the task is about sounds.
- Do not break SD profiles or UF2 service workflow.
- Avoid broad formatting-only changes.
- Prefer small, reviewable changes.
- Commit only when the user asks for a commit.
- Do not push unless the user explicitly asks.

## Done Criteria

For NightKite Link changes:
- `platformio run` succeeds when source changes are made.
- `platformio run -e cardputer` succeeds when the environment exists and source changes are made.
- `git diff --check` is clean.
- USB legacy workflow still works.
- USB NK4 still works.
- Firmware 4.0 features are capability-detected and tolerate missing/unsupported fields.
- UI remains readable on the Cardputer display.
- SD profile behavior remains backward-compatible.
- Docs are updated when user-facing behavior changes.
