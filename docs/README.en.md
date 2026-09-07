# NightKite Link Documentation

## Show Control runtime

The new **Show** carousel card provides ARM/READY status, ALL/GROUP/SINGLE targets,
patterns 1–27, runtime brightness, static colors, blackout/release and SD playback.
`C` selects a field, `+/-` changes it, Enter applies. Quick keys: `A` arm/disarm,
`W` white, `B` blackout, `R` release, `X` stop, `F` scan files while OFF. Load
`/shows/*.nks` before ARM; wait for LOADED and READY, then PLAY. SINGLE selection
reuses controller short IDs from the existing BLE scan or USB identity.

Shows are authored externally. Files are versioned text, fully validated before
streamed playback. STOP drains already sent events before ALL RELEASE; use
Stop/Restart, with no pause/resume. Patterns 23–27 require fresh Audio V2.
Receivers need current Show V1 firmware and saved `wireless_enabled=1` plus
`show_control enabled=1`. Keep matching audio groups and no GATT connection.

PC software uses the `NKSHOW 1` USB API, not the BLE contract. Select PC Bridge in Show's USB role field while disarmed, then restart with USB
already connected to the PC. Late USB attachment is currently unsupported. The mode
is saved; Controller Host is the default. Unplug the PC before rebooting into Host. Tab5 shares the API/player
on its USB-C console, without an added touch editor. See the
[complete sender, file and USB API reference](SHOW_CONTROL.md),
[example file](../examples/demo.nks) and [Python demo](../tools/show_bridge_example.py).

## Project Overview

NightKite Link is a compact configurator and service device for NightKite Multi
controllers. The established M5Stack Cardputer-Adv / StampS3 target remains the
fully functional handheld. The M5Stack Tab5 is a second target with a separate
1280 x 720 touch UI. It supports USB and BLE NK4 controller selection,
configuration, profiles, patterns, playback, sync, Audio Beacon, diagnostics,
SD and UF2 service.

The goal is to configure and service a NightKite controller without opening a
laptop. NightKite Link uses the existing NightKite USB CLI where possible and
does not require changes to the NightKite Multi firmware for the normal
configuration workflow.

The Cardputer UI remains card-based for its small display. The Tab5 UI is kept
separate so it can use touch and the larger screen without conditionals in the
Cardputer views.

## Multi-target architecture

- `src/main.cpp` is the Cardputer UI and hardware integration.
- `src/targets/tab5/main.cpp` is the Tab5 touch UI and hardware integration.
- `include/` and the portable sources in `src/` are shared: NK4/legacy command
  construction and UUIDs, profile codec, queue/session policies, controller
  state parsing, audio-sync DSP, beacon codec and UF2 validation.
- Tab5 uses Arduino as an ESP-IDF component. Its local `sdkconfig.defaults`
  enables P4 PSRAM and ESP-Hosted/NimBLE over the onboard C6 without changing
  the Cardputer SDK configuration. `dependencies.lock` pins the resolved IDF
  components.

The P4 has no native Bluetooth controller. BLE GATT central operation, scanning
and legacy advertising therefore run through the C6 over ESP-Hosted SDIO/VHCI.
This path is supported by the selected Arduino/ESP-IDF toolchain and is compiled
into the Tab5 target. Wi-Fi is not used by NightKite Link. If the C6 reports
version `0.0.0`, Link stops cleanly before NimBLE initialization. The M5Stack
factory image restores Wi-Fi/SDIO operation but does not enable the Hosted BLE
path. Use it as the recovery baseline, then install a co-processor image matching
the ESP-Hosted version pinned by the Tab5 build through the same internal C6
download header and USB-to-TTL adapter. Link rejects versions older than 2.6.
See the [M5Stack procedure](https://docs.m5stack.com/en/guide/restore_factory/m5tab5_c6_wifi)
for access and wiring of the internal header.

## Features

Implemented or present in the current code:

- Cardputer battery display, including charge state
- PCM-based startup, key, navigation, confirm, cancel, success and error sounds
- Local Link Options card for sound, volume, key/startup sounds, and display brightness
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
- Sync Setup Test card for preparing Firmware 4.0 master/follower beacon tests
- Experimental Audio Beacon card with V1/V2 manual and Cardputer microphone audio-sync modes
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

- M5Stack Cardputer-Adv / StampS3 or M5Stack Tab5
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

The experimental Audio Beacon card broadcasts NightKite Sync Beacon V1 or V2
as a Cardputer Beacon Master using non-connectable BLE manufacturer data. V1
continues to work with unchanged Firmware 4.0 controllers. V2 audio-test mode
requires NightKite Multi firmware with V2 receive support (`4cfa6a0` or newer).
Audio-sync patterns 23 through 27 require NightKite Multi `0f03e9e` or newer.
The mode is separate from BLE GATT configuration and does not keep a controller
GATT connection open while broadcasting. Available modes are `V1 Manual`,
`V2 Manual`, `V2 Mic Energy` and `V2 Mic Full`. Mic Energy controls energy and
confidence from the microphone while retaining the manual band values. Mic Full
also controls bass, mid and treble and enables simple beat/BPM/phase tracking.

To test it, configure one or more NightKite Multi controllers as followers:
`play_mode=sync`, `sync_enabled=1`, `sync_role=follower`, matching
`sync_group` 1-4, and `wireless_enabled=1`. Then open the Audio Beacon card,
choose the same group plus pattern, brightness and BPM, and press `Enter` to
start or stop broadcasting. Select V1 for the established path, V2 Manual for
manual test values, or a Mic mode for live analysis. Mic fields provide
sensitivity, noise gate, smoothing, beat detect and pause. Tap tempo remains the
fallback when beat tracking is disabled or uncertain.

Capture runs asynchronously at 8 kHz with 256-sample/32-ms mono frames. The DSP
removes DC, tracks RMS, peak and a slow noise floor, applies a gate and
attack/release smoothing, then normalizes energy. Mic Full uses a small Goertzel
filter bank covering approximately 60-250 Hz, 250-2000 Hz and 2000-3400 Hz.
The upper treble limit follows the 4-kHz Nyquist limit. Speaker UI sounds are
suspended while capture is active. V2 remains a 22-byte payload in a 29-byte
legacy advertisement without local name or service data.

The Cardputer catalog now covers all 27 firmware patterns. The new entries are
`audio_pulse_angle_color` (23), `audio_spectrum_ribbon` (24),
`audio_beat_ripples` (25), `audio_band_comets` (26), and `audio_beat_mosaic`
(27). These IDs are selectable in V1 Manual, V2 Manual, V2 Mic Energy, and V2
Mic Full. Firmware 4.0 controllers report their pattern count through NK4;
Firmware 3.x legacy configuration and bulk actions remain limited to 22.

Controller diagnostics:

```text
NK4 seq=10 cmd=get section=sync
NK4 seq=20 cmd=audio_sync_status
```

Hardware test:

1. Configure the controller as `play_mode=sync`, `sync_enabled=1`,
   `sync_role=follower`, with matching `sync_group` and `wireless_enabled=1`.
2. Start `V2 Mic Full`, play music or a regular pulse near the Cardputer, and
   select patterns 23, 24, 25, 26, and 27 in sequence.
3. Run the two commands above over controller USB for each pattern.
4. Verify `sync_locked=1`, `local_pattern` or `sync_pattern` matching 23-27,
   `audio_valid=1`, `last_beacon_version=2`, increasing `scan_decode_v2`,
   responsive energy/bands, improving confidence with a stable pulse,
   plausible `audio_beat_ms`, and `scan_crc_fail=0`.

## Build and Upload

Clone the repository:

```sh
git clone https://github.com/SunnyCodes86/nightkite-link.git
cd nightkite-link
```

Open the folder in VS Code / PlatformIO, or use the PlatformIO CLI directly.

The configured PlatformIO environments are `cardputer` and `tab5`. Build them
through the target wrapper so their different pinned framework packages remain
isolated.

Build:

```sh
python3 scripts/pio_target.py all
```

Upload:

```sh
python3 scripts/pio_target.py cardputer -t upload
python3 scripts/pio_target.py tab5 -t upload
```

Serial monitor:

```sh
python3 scripts/pio_target.py cardputer -t monitor
python3 scripts/pio_target.py tab5 -t monitor
```

Current targets from `platformio.ini`:

- Cardputer: pioarduino `54.03.21-2`, Arduino 3.2.1 / ESP-IDF 5.4.2;
  `m5stack-stamps3`
- Tab5: pioarduino `55.03.37`, Arduino as an ESP-IDF 5.5.2 component;
  `m5stack-tab5-p4`
- monitor speed: `115200`

ESP-IDF 5.5.x currently regresses the Cardputer-Adv ES8311 legacy-I2S MCLK
path (Espressif issue #18621) and yields constant microphone samples. The
wrapper gives each target its own `.pio/core-*` package directory so building
one cannot replace the other's Arduino/IDF packages.

## Tab5 workflow and hardware diagnostics

A persistent navigation rail divides the touch workflow into `Connect`,
`Control`, `Patterns`, `Playback`, `Sync`, `Audio`, `Profiles`, `Controller`,
`Service`, and `Firmware`. `Connect` selects USB or starts a BLE scan;
discovered BLE controllers can be tapped directly. After the NK4 handshake and
complete initial refresh, the UI provides:

- separate drafts and explicit `Apply & Save` for pattern/brightness, playback,
  pattern masks, and bulk edits
- sync role, group, master UID, loss behavior, radio state, and radio profile
- manual or microphone-driven V1/V2 Audio Beacon with tap tempo, manual V2
  energy/bands/confidence, beat, BPM, sequence, and advertising status
- profile create/overwrite/load/live-apply/rename/confirmed-delete; profile apply
  deliberately does not persist controller state
- controller name, strip length, smoothing, sensor ranges, boot calibration,
  confirmed factory defaults, and explicit persistent save
- service tabs for USB-only quick/precise calibration, timing/sensor refresh, a
  guarded NK4 terminal, live sync/radio/BLE diagnostics, SD checks, and all
  persistent local sound, volume, touch/startup tone, and display options
- full UF2 validation, RP2040/RP2350 target selection, confirmation, byte/percent
  progress, and cancellation in the firmware workflow

`Reload` asks before discarding local drafts and refreshing them; live profile
apply also requires confirmation and remains deliberately non-persistent.
`Disconnect` clears the
session, queue, and derived state. Busy, timeout, protocol, queue, and
disconnect errors remain visible and cannot be reported as a successful save.
The terminal accepts one NK4 `cmd=` line; `save` and `defaults` are blocked there
and remain available only through their confirmed UI workflows.

The serial monitor accepts `status`, `reload`, `sd`, `audio`, `usb`, `gatt`, `beacon` and
`all`. Display and shared core are checked at boot. `reload` uses the same safe
queue refresh as the touch action; `sd` mounts the card through
4-bit SDMMC; `audio` plays an audible 4 kHz test tone and then validates microphone input;
`usb` waits for an NK4 controller; `gatt` scans, connects the first match and
checks read/write/notify through the complete initial refresh. `beacon` sends a
valid NightKite V1 group-1 sync beacon for three seconds, so run it only with
the intended follower in range. An active GATT session blocks advertising; a
GATT scan stops an active diagnostic advertiser. USB may remain connected while
advertising. `all` starts only the local SD/audio checks and USB path; radio checks
remain separate because they switch transports.

Hardware validation confirms the ST7121 display at 1280 x 720, edge-to-edge
touch alignment, 4-bit SDMMC, speaker and microphone, USB NK4 including
write/save/reload and reconnect, and ESP-Hosted 2.12.11 with BLE scan, GATT
connection, NK4 read/write/notify and controller reception of the sync beacon.
A 10:21-minute GATT run with controller USB active completed eleven full
refreshes without timeout or reconnect. A separate run completed 16 beacon
windows and 16 parallel USB refreshes without errors; the controller decoded
170 beacons with no decode, CRC, or group failures. The Tab5 controller host
path is physically the USB-A connector. USB-C uses the separate USB-device data
pair with fixed device-role CC resistors and cannot directly host a NightKite
controller. The persistent header shows Tab5 battery level and active charging
state plus the battery level reported by the connected controller. The ST7123
display-controller revision and a real UF2 write including BOOTSEL target
matching, disconnect and reboot on one RP2040 and one RP2350 device remain open.
Parser, family validation and target-mismatch handling are covered by automated
tests but do not confirm real flashing. On physical USB removal ESP-IDF's
endpoint cleanup prints two `ESP_ERR_INVALID_STATE` messages after `DEV_GONE`
was already handled. The application still returns cleanly to its waiting state
without a phantom connection; suppressing this cosmetic output would require a
risky framework patch and is intentionally avoided.

A few differences from Cardputer remain deliberate. The Tab5 controller UI
requires Firmware 4.x/NK4; the Firmware 3.x legacy USB path remains unchanged
on Cardputer. Compact sync-test shortcuts are not copied one-for-one because
the same actions are available through the larger Sync, Control, Playback and
Diagnostics workflows. Cardputer keyboard/PCM feedback is represented by Tab5
touch tones using the same persistent sound settings. The complete mapping is
maintained in [the Cardputer/Tab5 function matrix](TAB5_FUNCTION_MATRIX.md).
The expanded Tab5 workflows still need the bundled hardware pass below.

### Bundled final hardware test

The expanded UI is not reflashed and tested piecemeal after every feature. The
final hardware pass instead bundles:

1. Flash the final Tab5 build; verify boot, both display-controller revisions
   where available, full touch alignment, navigation, persisted sound/display
   options, startup/touch/status tones, and both battery indicators.
2. Connect over USB NK4; exercise initial refresh, Control, Playback, pattern
   masks/bulk, Controller, Calibration, Terminal, and Sync through apply, save,
   confirmed dirty reload, physical disconnect, and reconnect while observing
   queue/busy/controlled-error states.
3. On SD, create, overwrite, load, live-apply, explicitly save, rename, and
   delete a profile. Also test malformed/oversized/interrupted writes and `.bak`
   recovery.
4. Repeat representative read/write/save workflows over BLE, including scan,
   connect, read, write, notify, automatic diagnostic polling, a long sync
   response, and clean disconnect.
5. Disconnect GATT, configure a follower, and test Audio Beacon V1/V2, Manual
   including tap tempo and V2 bands/confidence, Mic Energy, and Mic Full. Run
   parallel USB refreshes and inspect lock, decode,
   CRC, audio, and advertising counters; then verify speaker/microphone
   diagnostics and microphone pause.
6. Exercise calibration and the guarded terminal. Validate correct and wrong
   RP2040/RP2350 UF2 files, reject a mismatched BOOTSEL target, and perform one
   real flash per family including progress, disconnect/reboot, and controlled
   cancellation.

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
- are selected from `Firmware Update`

Profiles:

- are stored under `/profiles/`
- use `.json`
- new profiles are named `profile_001.json` through `profile_999.json`

## Profile Format

Profiles are written and parsed with a bounded JSON decoder. Malformed,
truncated, oversized, unsupported-version, wrong-type and out-of-range profiles
are rejected before they can replace the loaded profile state.

Current saved structure. `profile_version: 2` adds optional Firmware 4.0 fields.
Older profiles remain readable; missing keys keep the current/default value.
When compact pattern masks are absent, `patterns[].cycle_enabled` and
`patterns[].inverted` are used as the compatibility fallback. Profile saves are
written to a temporary file and verified before replacement; an interrupted
overwrite keeps a recoverable `.bak` copy that is restored on the next profile
scan.

A loaded profile can be applied only after the current USB or BLE controller
session has completed its initial identity, status, configuration and play-state
refresh. Disconnecting during the confirmation step cancels the apply.

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
    "enabled_pattern_mask": 134217727,
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
then re-apply the inverted pattern list. Existing profiles containing IDs 1
through 22 remain compatible. When applying a profile to an older controller,
masks and active pattern are defensively limited to the reported or legacy
pattern range.

## Controls

The current keyboard handling processes these controls:

| Key | Action |
| --- | --- |
| Left / right arrow | Previous / next card |
| Up / down arrow | Edit value or move selection |
| `A` / `D`, `W` / `S` | Fallback aliases for the corresponding arrow keys |
| `Enter` | Apply, open, confirm, or continue in flash workflow |
| `Backspace` / `DEL` | Back or cancel where supported |
| `Tab` | Next card |
| `R` | Refresh current card/controller data where implemented |
| `C` | Select editable field, toggle firmware target, or toggle pattern cycle depending on card |
| `T` | Tap tempo on the Audio Beacon card |
| `I` | Toggle pattern invert, or delete selected profile on the Profiles card |

The physical Cardputer arrow keys produce the punctuation aliases used by the
keyboard library. Footer hints therefore show compact ASCII arrows first and
only include the highest-priority complete hints that fit the 240 px width.

During critical firmware copy states, normal card navigation is locked. Cancel
is only accepted in safe flash states.

Editable cards keep a local draft while a field is marked pending. Automatic
controller refreshes continue updating the controller state, but they do not
overwrite the active draft. Pending fields are marked with `*`; press `Enter` to
apply them or `Backspace` / `DEL` to discard the local edit.

## UI Concept

NightKite Link uses a card-based interface instead of a classic large menu
because the display is only 240 x 135 px. The flat order keeps live controls at
the front and diagnostics/service at the back: `Status`, `Pattern Live`,
`Brightness`, `Play`, `Audio Beacon`, `Patterns`, `Pattern Bulk`, `Profiles`,
`Link Options`, `Controller`, `BLE Connect`, `Controller Setup`, `Controller Sync`,
`Controller Radio`, `Motion Service`, `Sync Diagnostics`, `Sync Setup Test`,
and `Firmware Update`.

On `Patterns`, up/down changes the controller pattern as a live preview.
`Enter` opens the cycle/invert detail view.

The `Link Options` card changes only the Cardputer/Link itself. `C` selects
sound enable, volume, key sounds, startup sound, display brightness, or local
reset; up/down applies the selected value immediately. Reset restores only
these Link defaults. It does not send a controller command, invoke controller
save/defaults, or alter SD profiles.

Local settings use a versioned, checksummed record in the ESP32 Preferences/NVS
namespace `nk-link`. Rapid edits are combined and written after one second, and
unchanged values are not rewritten. Values are validated during boot; an
invalid record falls back to defaults and is repaired. Sound defaults to on,
volume 210, key and startup sounds on, and display brightness 96.

The top status bar shows compact transport/protocol state (`USB LEG` or
`USB NK4`), compact queue state (`Q0`...`Q9+` or `Q!`), play/role tokens, controller battery
when available and Cardputer battery. The firmware flasher uses its own workflow
screens for confirmation, BOOTSEL instructions, waiting, progress, reboot and
error states.

The Controller card shows `Cfg repaired` when Firmware 4.x reports successful
persistent-config recovery. Controller battery warning states (`LOW`, `CRIT`,
`CUT`, `EMPTY`) take priority over the smoothed voltage/percentage display, so a
protection transition remains visible even while voltage display hysteresis is active.

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

Firmware 4.x may return BLE NK4 lines up to 4094 characters. Link accepts the
complete line and reassembles arbitrary TX Notify chunks through the terminating
newline. Sequence-echoed overflow errors are handled like any other matching
`err` response, so the pending command fails immediately instead of timing out.

In USB NK4 mode, automatic polling is intentionally light: Link polls `status`
periodically after the UI has been idle, while full section reads are reserved
for connect, manual refresh and successful apply follow-up reads.

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

On Firmware 4.x, `calibrate quick` and `calibrate precise` map to
`NK4 cmd=calibrate mode=quick|precise` over USB. Link keeps that request pending
for up to ten minutes because precise calibration is intentionally slow. The
blocking maintenance operation is not offered over BLE; Link reports that USB
is required.

In NK4 mode, existing UI actions are translated to NK4 requests such as
`cmd=set brightness=...`, `cmd=set play_mode=manual|autoplay|sync`,
`cmd=set sync_enabled=0|1`, `cmd=set sync_group=...`,
`cmd=set sync_role=standalone|master|follower`,
`cmd=set wireless_enabled=0|1`, `cmd=set wireless_profile=...`,
`cmd=set enabled_mask=...` and `cmd=set inverted_mask=...`.

The BLE NK4 service implemented by Firmware 4.0 can be used experimentally from
the BLE Connect card. NightKite Link scans for `NK-...` devices or the NightKite
service UUID, connects to one controller at a time, and uses the same NK4 parser
as USB. Scanning runs in the background so keyboard, audio and UI updates remain
active; connection attempts are bounded and can be retried after failure. TX
Notify chunks are reassembled until newline `\n`. USB remains the stable
recommended path. Link is a configurator and diagnostic tool; it does not relay
real-time sync beacons or stream LED frames.

Current controller firmware advertises the service UUID in the primary packet
and the `NK-...` name in the scan response. Link uses active scanning and accepts
either field, so this split remains discoverable. A sync master suppresses its
connectable GATT advertisement while it owns the radio; disconnect or leave
master beacon mode before scanning for BLE NK4.

Bulk invert currently maps to comma-separated `invert_pattern` /
`normal_pattern` commands. A code comment marks a future dedicated
`set all_patterns_invert` style command as a TODO if the controller firmware
adds one later.

## Two-Controller Sync Setup Test

For Firmware 4.0 USB NK4 controllers, the Sync Setup Test card provides a compact
setup and diagnostic workflow for the first master/follower beacon tests. It is
only a configurator and diagnostic view. BLE NK4 can configure controllers, but
it is not a real-time sync path and does not relay sync traffic.

Typical master setup:

1. Connect controller A over USB and confirm `USB NK4`.
2. Open Sync Setup Test.
3. Select the group, usually `Group 1`, and wireless profile, usually
   `balanced`.
4. Run `Configure Master`.
5. Run `Save`.

`Configure Master` queues:

- `set name=NK-Master`
- `set play_mode=sync`
- `set sync_enabled=1 sync_group=<group> sync_role=master`
- `set wireless_enabled=1 wireless_profile=<profile>`

Typical follower setup:

1. Connect controller B over USB and confirm `USB NK4`.
2. Open Sync Setup Test.
3. Use the same group and wireless profile as the master.
4. Run `Configure Follower`.
5. Run `Save`.

`Configure Follower` queues:

- `set name=NK-Follower`
- `set play_mode=sync`
- `set sync_enabled=1 sync_group=<group> sync_role=follower`
- `set wireless_enabled=1 wireless_profile=<profile>`

`Refresh Sync` queues `get section=sync`, `sync_status`, `get section=wireless`
and `status`. While the Sync Setup Test card is open, Link polls `sync_status` about
every 1.8 seconds and `get section=wireless` about every 5 seconds, but the
existing dirty/draft protection still prevents active edits from being
overwritten.

The pattern list also shows compact Firmware 4.0 pattern classification:

- `S`: sync-ready
- `P`: partial-sync
- `L`: local/reactive
- `?`: classification unknown

The separate Sync Diagnostics card shows PatternClock and apply diagnostics such as
`drift_ms`, `phase_ms`, `beacon_phase_ms`, `last_beacon_seq`,
`last_applied_seq`, `sync_apply_count`, `sync_apply_skipped`,
`sync_apply_reason`, `last_pattern_change_latency_ms`, `sync_ready_pattern`,
`partial_sync_pattern`, `sync_autoplay`, `master_autoplay` and
`autoplay_next_ms`.

This makes master autoplay in sync mode visible and helps verify whether a
follower is applying received beacons. USB NK4 remains the stable diagnostic
path; BLE NK4 is available as an experimental configuration and diagnostic path.

Diagnostic fields are intentionally short for the Cardputer display:

- `radio_mode`: expected `beacon_master` on the master or `beacon_follower` on
  the follower when beacon sync is active.
- `beacon_tx_count`: transmitted beacon count; should rise on the master.
- `beacon_rx_count`: received beacon count; should rise on the follower.
- `beacon_crc_errors`: malformed beacon count; should stay low.
- `beacon_group_mismatch`: beacons ignored because the group differs.
- `beacon_age_ms`: age of the last received beacon, shown as `A...`.

If `radio_mode=gatt` is shown, a BLE GATT client is connected to the controller
and beacon sync is not active. Disconnect the BLE client before judging the
beacon test. Keeping USB connected to NightKite Link for configuration and
diagnostics is fine.

## Save And Factory Reset

Live changes such as brightness or active pattern are sent to the controller
immediately, but they are only persistent after `save`. On the Controller card,
`S save` is the explicit persistence action.

Pattern changes remain marked `UNSAVED` after their live command succeeds. The
marker is cleared only after the controller confirms the persistent `save`.
A failed/timed-out pattern command, partial batch failure, or change queued
after `save` leaves `UNSAVED` active even if that save later returns `ok`.

Rapid brightness and active-pattern live edits replace an older unsent edit of
the same kind. They never cross another user-command barrier. The queue holds at
most 64 waiting commands; background refreshes are deduplicated or dropped
first, while a user command that still cannot fit reports `Command queue full`.
NK4 multi-command success is shown only if every user command succeeds. Legacy
CLI batches are reported as sent because Legacy has no sequence-matched batch
confirmation.

`C reset USB` only resets Link's USB/protocol session. It is not a controller
factory reset.

`F defaults` opens a confirmation for controller factory defaults. After
confirmation, Link sends in USB NK4 mode:

- `defaults confirm=1`
- `save`
- `info`, `status`, `get section=config`, `get section=play`,
  `get section=sync`, `get section=wireless`, `get section=patterns`

Persistent Firmware 4.0 fields are: device name, brightness, active pattern,
strip length, smoothing, accel/gyro range, boot calibration, autoplay on/off,
autoplay interval, play mode, boot mode, enabled/inverted masks, sync enabled,
sync group, sync role, sync master UID, sync loss behavior, wireless enabled and
wireless profile.

Runtime diagnostics such as PatternClock phase, beacon counters/age, lock state,
apply counts, apply reason, pattern latency, battery status and connection state
are not persistent settings.

## Firmware Flasher

The firmware flasher works with UF2 files on the SD card and USB Mass Storage
mode on the RP2040/RP2350 controller.

Current flow:

1. Copy a `.uf2` firmware file to `/firmware/` on the SD card.
2. Open Firmware Update.
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
- every UF2 block has valid magic, payload size, numbering and block count
- every declared UF2 family matches the selected RP2040 or RP2350 target
- files without a family ID are rejected because they cannot be matched safely
- the connected BOOTSEL device has Raspberry Pi's USB VID and the expected
  RP2040 or RP2350 boot PID before any data is written
- all writes, VFS flush/close and unmount operations must succeed

Success is shown only after the full copy has completed and the matched BOOTSEL
device disconnects for reboot. A reboot timeout is reported as an error, not as
successful flashing.

Warnings:

- Do not unplug during copying.
- The selected target must match both the UF2 family and connected controller.
- This flasher is experimental / work in progress.
- This is a service/recovery workflow and not a normal NightKite CLI command.

## Troubleshooting

### Cardputer does not upload

- Check that the correct USB port is selected in PlatformIO.
- Use `python3 scripts/pio_target.py cardputer -t upload`.
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
- Refresh Firmware Update with `R`.

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

### Tab5 UI performance

The Tab5 UI now retains its 1280 x 720 canvas and redraws only coalesced dirty
regions for ordinary changes. Full frames are limited to page/modal transitions
and large combined changes. The physical display stays in native 720 x 1280
rotation while the PSRAM canvas preserves logical 1280 x 720 coordinates,
avoiding M5GFX's costly rotated full-frame transfer. Touch-down feedback is
transferred before tones, diagnostics, and deferred SD/profile/firmware work.
Measurements, toolchain and Kconfig/cache comparisons, and the LVGL
recommendation are in [TAB5_UI_PERFORMANCE.md](TAB5_UI_PERFORMANCE.md).

- The project is intentionally compact and targeted at a small handheld display.
- The UI should remain non-blocking; `M5Cardputer.update()` must run regularly.
- USB CLI communication and UF2 Mass Storage flashing should stay clearly
  separated.
- The bounded command queue preserves user-command order and sends commands
  with a short interval.
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
