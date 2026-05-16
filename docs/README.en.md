# NightKite Link Documentation

## Project Overview

NightKite Link is a compact handheld configurator and service device for
NightKite Multi controllers. It is built for the M5Stack Cardputer-Adv /
StampS3 target and communicates with the controller over USB host mode.

The goal is to configure and service a NightKite controller without opening a
laptop. NightKite Link uses the existing NightKite USB CLI where possible and
does not require changes to the NightKite Multi firmware for the normal
configuration workflow.

The UI is card-based because the Cardputer-Adv display is very small. One card
contains one primary function or a small group of related settings.

## Features

Implemented or present in the current code:

- Cardputer battery display, including charge state
- PCM-based startup, key, navigation, confirm, cancel, success and error sounds
- USB connection status
- Controller/CLI connection status and timeout handling
- Brightness display and immediate change/send behavior
- Strip length display and configurable draft value
- Active pattern display and immediate pattern switching
- Smoothing display and configurable draft value
- Accelerometer and gyro range configuration
- Motion service card with FPS display and calibration actions
- Autoplay enabled state and autoplay interval configuration
- Pattern list with cycle and invert state
- Pattern detail configuration with cycle and invert toggles
- Bulk pattern actions:
  - save all current pattern states
  - enable all patterns for cycle
  - disable all patterns for cycle
  - invert all patterns
  - set all patterns to normal
- SD card profiles:
  - save current controller settings
  - list JSON profiles
  - load a selected profile
  - apply a loaded profile to the controller
  - delete selected profiles
- Firmware area:
  - scans `.uf2` files from `/firmware/`
  - target selector for `RP2040` and `RP2350`
  - dedicated graphical flash workflow
  - UF2 validation before flashing
  - USB Mass Storage copy progress display

The UF2 Mass Storage flasher is currently **experimental / work in progress**.
It is intended as a service or recovery workflow, not as a normal CLI command.

UI sounds are enabled by default in code and use embedded/generated PCM data, so
no SD sound files are required.

## Hardware Requirements

- M5Stack Cardputer-Adv / StampS3 target
- microSD card
- USB-C cable and suitable USB-OTG setup
- NightKite Multi controller, for example based on a Pimoroni Pico LiPo 2 /
  RP2350
- UF2 firmware files for the firmware flasher

For UF2 flashing, the controller must be placed manually into BOOTSEL / USB Mass
Storage mode. The current code does not assume an automatic `reboot_bootsel`
command.

## Software Requirements

- PlatformIO
- VS Code with PlatformIO extension, or PlatformIO CLI
- Git
- USB drivers if required by the host system

Normal CLI configuration uses the existing NightKite USB CLI. No NightKite Multi
firmware changes are required for that path as long as the controller provides
the expected CLI commands.

## Build and Upload

Clone the repository:

```sh
git clone https://github.com/SunnyCodes86/nightkite-link.git
cd nightkite-link
```

Open the folder in VS Code / PlatformIO, or use the PlatformIO CLI directly.

The configured PlatformIO environment is `cardputer`.

Build:

```sh
pio run
```

Upload:

```sh
pio run -t upload
```

Serial monitor:

```sh
pio device monitor
```

Current target from `platformio.ini`:

- platform: `pioarduino/platform-espressif32` 51.03.03 package URL
- board: `m5stack-stamps3`
- framework: `arduino`
- monitor speed: `115200`

## SD Card Layout

The code expects these directories on the microSD card. They are created when SD
initialization succeeds:

```text
/firmware/
  nightkite_multi_rp2350_v3.uf2

/profiles/
  profile_001.json
  profile_002.json
```

Firmware files:

- must be placed under `/firmware/`
- must use the `.uf2` extension
- are selected from the Firmware card

Profiles:

- are stored under `/profiles/`
- use `.json`
- new profiles are named `profile_001.json` through `profile_999.json`

## Profile Format

Profiles are written as JSON without ArduinoJson. Loading is intentionally
simple and reads the keys used by the current code.

Current saved structure. `profile_version: 2` adds optional Firmware 4.0 fields.
Older profiles remain readable; missing keys keep the current/default value.

```json
{
  "profile_version": 2,
  "project": "NightKite Link",
  "target": "NightKite Multi",
  "settings": {
    "device_name": "NK-Test",
    "brightness": 159,
    "strip_length": 50,
    "active_pattern": 7,
    "smoothing": 45,
    "accel_range": 4,
    "gyro_range": 500,
    "play_mode": "manual",
    "boot_mode": "last",
    "sync_enabled": false,
    "sync_group": 1,
    "sync_role": "standalone",
    "sync_master_uid": "",
    "sync_loss_behavior": "continue_local",
    "wireless_enabled": false,
    "wireless_profile": "balanced",
    "enabled_pattern_mask": 4194303,
    "inverted_pattern_mask": 0,
    "autoplay": {
      "enabled": true,
      "interval_seconds": 30
    },
    "patterns": [
      {
        "id": 1,
        "name": "Rainbow",
        "cycle_enabled": true,
        "inverted": false
      }
    ]
  }
}
```

When applying a loaded profile to a Firmware 4.0/NK4 controller, NightKite Link
prefers compact NK4 `set` commands, including `enabled_mask` and
`inverted_mask`. In legacy mode it keeps the existing Firmware 3.x command flow:
scalar settings, pattern enable/disable lists, reset all patterns to normal, and
then re-apply the inverted pattern list.

## Controls

The current keyboard handling processes these controls:

| Key | Action |
| --- | --- |
| `A` / `D` | Previous / next card |
| `W` / `S` | Change value or move selection |
| `Enter` | Apply, open, confirm, or continue in flash workflow |
| `Backspace` / `DEL` | Back or cancel where supported |
| `Tab` | Next card |
| `R` | Refresh current card/controller data where implemented |
| `C` | Select editable field, toggle firmware target, or toggle pattern cycle depending on card |
| `I` | Toggle pattern invert, or delete selected profile on the Profiles card |
| `,` / `<` | Previous card |
| `/` / `?` | Next card |
| `;` / `:` | Same direction as `W` |
| `.` / `>` | Same direction as `S` |

During critical firmware copy states, normal card navigation is locked. Cancel
is only accepted in safe flash states.

## UI Concept

NightKite Link uses a card-based interface instead of a classic large menu
because the display is only 240 x 135 px. Each card presents one primary task:
status/device, play mode, sync, wireless diagnostics, brightness, configuration,
calibration, pattern selection, pattern list, bulk actions, firmware, or
profiles.

The top status bar shows compact transport/protocol state (`USB LEG` or
`USB NK4`), controller name or short ID, play/role tokens, controller battery
when available and Cardputer battery. The firmware flasher uses its own workflow
screens for confirmation, BOOTSEL instructions, waiting, progress, reboot and
error states.

## Controller Communication

NightKite Link uses `USBHostSerial` in USB host mode when `NIGHTKITE_USB_HOST=1`
is enabled. A debug serial transport exists for non-host builds.

On USB connect, Link first attempts Firmware 4.0/NK4:

1. Send `protocol machine`.
2. Send `NK4 seq=<id> cmd=hello client=nightkite-link proto_min=4 proto_max=4`.
3. If a valid NK4 response arrives, switch to USB NK4 and query
   `info`, `caps`, `status`, `get section=config`, `get section=play`,
   `get section=sync`, `get section=wireless` and `get section=patterns`.
4. If NK4 times out, fall back to the existing USB legacy CLI.

The NK4 parser handles `ok`, `err` and `event` lines, matches `seq`, tolerates
unknown keys and uses timeouts so the UI does not freeze.

The parser handles:

- `OK key=value ...`
- `ERR ...`
- `INFO ...`
- `[NightKite CLI] ...`
- `NK4 seq=<id> ok key=value ...`
- `NK4 seq=<id> err code=<code> msg=<message>`
- `NK4 event=<name> key=value ...`

The code updates controller state from keys such as:

- `pattern`
- `brightness`
- `strip_length`
- `smoothing`
- `accel_range`
- `gyro_range`
- `autoplay`
- `autoplay_interval`
- `enabled_patterns`
- `inverted_patterns`
- `battery_voltage`
- `boot_calibration`
- `fps`

Commands currently sent by the code include:

- `show`
- `patterns`
- `get inverted_patterns`
- `set brightness <value>`
- `set strip_length <value>`
- `set pattern <id>`
- `set smoothing <value>`
- `set accel_range <value>`
- `set gyro_range <value>`
- `set autoplay on|off`
- `set autoplay_interval <seconds>`
- `enable_pattern <id or comma-list>`
- `disable_pattern <id or comma-list>`
- `invert_pattern <id or comma-list>`
- `normal_pattern <id or comma-list>`
- `timing`
- `calibrate quick`
- `calibrate precise`
- `set boot_calibration quick|off`
- `save`

In NK4 mode, existing UI actions are translated to NK4 requests such as
`cmd=set brightness=...`, `cmd=set play_mode=manual|autoplay|sync`,
`cmd=set sync_enabled=0|1`, `cmd=set sync_group=...`,
`cmd=set sync_role=standalone|master|follower`,
`cmd=set wireless_enabled=0|1`, `cmd=set wireless_profile=...`,
`cmd=set enabled_mask=...` and `cmd=set inverted_mask=...`.

The BLE NK4 service implemented by Firmware 4.0 is not used by this version of
NightKite Link. USB remains the stable path. Link is a configurator and
diagnostic tool; it does not relay real-time sync beacons or stream LED frames.

Bulk invert currently maps to comma-separated `invert_pattern` /
`normal_pattern` commands. A code comment marks a future dedicated
`set all_patterns_invert` style command as a TODO if the controller firmware
adds one later.

## Firmware Flasher

The firmware flasher works with UF2 files on the SD card and USB Mass Storage
mode on the RP2040/RP2350 controller.

Current flow:

1. Copy a `.uf2` firmware file to `/firmware/` on the SD card.
2. Open the Firmware card.
3. Select the UF2 file with `W` / `S`.
4. Select the target label with `C` (`RP2040` or `RP2350`).
5. Press `Enter`.
6. Confirm the selected file.
7. Put the controller manually into BOOTSEL mode.
8. Reconnect it so it appears as a USB Mass Storage device.
9. Press `Enter` to continue.
10. NightKite Link waits for Mass Storage, mounts it at `/usb`, and copies the
    UF2 as `/usb/FIRMWARE.UF2`.
11. The UI shows progress, copied KB and percentage.
12. After the copy, the VFS is unmounted and the UI waits for reboot/disconnect.
13. Success or error is shown on a dedicated screen.

UF2 validation checks:

- file exists
- file size is greater than zero
- file size is divisible by 512
- first UF2 block magic values are valid

Family ID detection is present, but target-specific family validation is marked
as a TODO in the code.

Warnings:

- Do not unplug during copying.
- Use only UF2 files intended for the selected controller family.
- This flasher is experimental / work in progress.
- This is a service/recovery workflow and not a normal NightKite CLI command.

## Troubleshooting

### Cardputer does not upload

- Check that the correct USB port is selected in PlatformIO.
- Use `pio run -t upload`.
- If upload fails, try reconnecting the Cardputer-Adv.

### Controller is not detected

- Check the USB-OTG cable and controller power.
- The status bar should show USB connection state.
- Use `R` on relevant cards to refresh.

### `USB disconnected`

- The USB host transport lost the controller.
- Reconnect the controller.
- Pending queued commands are cleared by the code.

### `Controller timeout`

- USB may still be physically connected, but the CLI did not answer within the
  configured stale timeout.
- Reconnect the controller or send a refresh.

### SD card not ready

- Check that the microSD card is inserted and formatted with a filesystem
  supported by the Arduino SD library.
- The app creates `/profiles/` and `/firmware/` when possible.

### No UF2 file found

- Put `.uf2` files under `/firmware/`.
- Refresh the Firmware card with `R`.

### Invalid UF2

- The selected file failed basic UF2 validation.
- Check that it is a real UF2 file and not a renamed binary.

### Mass Storage timeout

- The controller did not appear as a USB Mass Storage device in time.
- Put the RP2040/RP2350 into BOOTSEL mode manually and reconnect USB.

### Mount failed

- The device was detected, but FAT/VFS mounting failed.
- Reconnect the controller in BOOTSEL mode and retry.

### Write failed

- The UF2 copy failed.
- Do not unplug during copy. Retry with a known-good UF2 and cable.

### After firmware flashing

- Let the controller reboot.
- Reconnect it normally so the NightKite USB CLI is available again.

## Development Notes

- The project is intentionally compact and targeted at a small handheld display.
- The UI should remain non-blocking; `M5Cardputer.update()` must run regularly.
- USB CLI communication and UF2 Mass Storage flashing should stay clearly
  separated.
- The current command queue sends CLI commands with a short interval.
- The firmware flasher pauses normal CLI polling while active.
- The M5Cardputer library is patched by `scripts/patch_m5cardputer.py` during
  build to add a missing GPIO include in the dependency if needed.

Possible future improvements:

- More robust UF2 flasher behavior on edge cases
- Optional `reboot_bootsel` support if NightKite Multi adds it later
- Optional BIN/ELF/Picoboot support later; not a current core goal
- Better profile parsing and validation
- Release workflow with prebuilt firmware binaries

## Roadmap

- UI polish for small-screen readability
- More robust profile management
- Stabilize and test the firmware flasher on real RP2040/RP2350 boards
- Document NightKite CLI compatibility more formally
- Add a release workflow with ready-to-flash builds
- Optional automatic firmware version detection
- Optional support for additional controller targets

## License

No license file has been added yet.

The vendored Espressif `usb_host_msc` component includes its own license file
under `lib/usb_host_msc/LICENCE`.

## Links

- NightKite Multi: https://github.com/SunnyCodes86/nightkite-multi
- NightKite Link: https://github.com/SunnyCodes86/nightkite-link
- M5Cardputer Library: https://github.com/m5stack/M5Cardputer
- Cardputer-Adv Documentation: https://docs.m5stack.com/en/core/Cardputer-Adv
- ESP USB MSC Host Component: https://components.espressif.com/components/espressif/usb_host_msc
- ESP-IDF USB Host: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_host.html
- PlatformIO: https://platformio.org/
