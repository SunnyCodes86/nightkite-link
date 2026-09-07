# NightKite Link

NightKite Link is an open-source configurator and service device for
[NightKite Multi](https://github.com/SunnyCodes86/nightkite-multi). The fully
functional Cardputer-Adv target keeps its 240 x 135 px card UI. A second M5Stack
Tab5 target provides a separate 1280 x 720 touch UI with USB/BLE NK4 controller
selection, configuration, profiles, sync/audio, diagnostics, SD and UF2 service
workflows.

Full English documentation: [docs/README.en.md](docs/README.en.md)

NightKite Link ist ein Open-Source-Gerät zum Konfigurieren und Warten von
[NightKite Multi](https://github.com/SunnyCodes86/nightkite-multi). Das
vollständig funktionsfähige Cardputer-Adv-Target behält seine 240-x-135-Pixel-
Card-Oberfläche. Ein zweites M5Stack-Tab5-Target stellt eine Touch-optimierte
1280-x-720-Oberfläche mit USB-/BLE-NK4-Controllerauswahl, Konfiguration,
Profilen, Sync/Audio, Diagnosen sowie SD- und UF2-Serviceabläufen bereit.

Ausführliche deutsche Dokumentation: [docs/README.de.md](docs/README.de.md)

## Features

- Card-based UI with compact status bar
- WAV/PCM-style startup and UI key sounds
- Cardputer battery, USB and controller/CLI status display
- USB NK4 detection for Firmware 4.0 with legacy CLI fallback for Firmware 3.x
- Controller, play mode, sync and controller-radio configuration cards
- Sync Setup Test card for preparing master/follower two-controller beacon tests
- Sync Diagnostics card for Firmware 4.0 PatternClock and beacon apply diagnostics
- Experimental Audio Beacon card with V1/V2 manual and Cardputer microphone audio-sync modes
- Brightness, strip length, active pattern, smoothing and autoplay settings
- Pattern list with cycle, invert and Firmware 4.0 sync classification state
- Pattern detail and bulk actions
- SD card profiles under `/profiles/`
- UF2 firmware selection from `/firmware/`
- Experimental USB Mass Storage UF2 flasher workflow for RP2040/RP2350 BOOTSEL mode

The flat Cardputer navigation is ordered by task frequency:
`Status`, `Pattern Live`, `Brightness`, `Play`, `Audio Beacon`, `Patterns`,
`Pattern Bulk`, `Profiles`, `Controller`, `BLE Connect`, `Controller Setup`,
`Controller Sync`, `Controller Radio`, `Motion Service`, `Sync Diagnostics`,
`Sync Setup Test`, and `Firmware Update`.

## Targets and architecture

- `cardputer`: ESP32-S3, keyboard, 240 x 135 display; the existing production UI
  and every current Link function remain here.
- `tab5`: ESP32-P4, 1280 x 720 touch display and ESP32-C6 radio coprocessor; the
  current image provides ten task-oriented touch pages for USB/BLE NK4 access,
  controller settings, playback, patterns, sync/audio, profiles, diagnostics,
  SD and UF2 management. It also contains focused display, touch, SD,
  speaker/microphone, USB-host, GATT and advertising checks.
- Shared `include/` and `src/` code owns commands/protocol constants, profiles,
  controller/session and queue policy, audio DSP, beacon encoding, battery
  parsing and UF2 validation. Hardware access and UI entry points stay under
  their target implementation. The Tab5 ESP-IDF component source list keeps
  target selection out of scattered preprocessor branches.

## Show Control

Show Control V1 adds a **Show** runtime card, an SD player (`/shows/*.nks`) and
a versioned USB gateway API for PC software. Live keys, files and USB share one
timed broadcast engine, alongside Audio V2. Configure and save receiver wireless
and show reception first. On Cardputer, select the saved PC Bridge mode in Show while disarmed; connect USB to the PC
before starting Cardputer. Late USB attachment is currently unsupported.
Controller Host remains the default; unplug the PC before rebooting into Host.
See the [sender/file/API reference](docs/SHOW_CONTROL.md),
[example show](examples/demo.nks) and [Python bridge demo](tools/show_bridge_example.py).
Tab5 uses the same engine/API via USB-C, without a new touch show editor.

Show Control V1 ergänzt eine **Show**-Karte, SD-Wiedergabe (`/shows/*.nks`) und
eine USB-Gateway-API. Empfangsfreigaben am Controller vorher speichern; PC Bridge
in der Show-Karte nach Disarm wählen und speichern. USB vor dem Cardputer-Start
mit dem PC verbinden; späteres Anstecken wird derzeit nicht zuverlässig erkannt. Controller Host
bleibt Standard; vor dem Neustart in Host den PC abziehen. STOP lässt bereits gesendete
Termine auslaufen und sendet anschließend RELEASE; es gibt kein Pause/Resume.

## Build

PlatformIO environments: `cardputer`, `tab5`. Their pinned framework packages
are isolated because Cardputer-Adv requires ESP-IDF 5.4.2 for working ES8311
microphone capture while Tab5 remains on ESP-IDF 5.5.2.

```sh
python3 scripts/pio_target.py all
python3 scripts/pio_target.py cardputer -t upload
python3 scripts/pio_target.py tab5 -t upload
python3 scripts/pio_target.py cardputer -t monitor
python3 scripts/pio_target.py tab5 -t monitor
```

## Current Status

The project is functional but still evolving. The USB CLI configuration workflow
and card UI remain the stable operating path. USB NK4 support adds Firmware 4.0
identity, play mode, sync and wireless configuration. The experimental BLE Connect
card can connect to Firmware 4.0 RM2/BLE controllers and use the same NK4 command
path over GATT. BLE GATT remains for configuration/status/control only; the
experimental Audio Beacon card is a separate Beacon Master show mode that sends
NightKite Sync Beacon V1 or V2 non-connectable BLE advertisements directly from
the Cardputer. V1 remains compatible with unchanged NightKite Multi controllers;
V2 audio-test mode requires controller firmware with V2 receive support
(`4cfa6a0` or newer). Audio-sync patterns 23-27 require NightKite Multi
`0f03e9e` or newer. Broadcasting does not keep a BLE-GATT controller
connection open. The modes are `V1 Manual`, `V2 Manual`, `V2 Mic Energy` and
`V2 Mic Full`. Mic Energy derives energy and confidence from the Cardputer
microphone. Mic Full additionally analyzes bass, mid and treble and performs
simple beat/BPM/phase tracking. The manual BPM or tap tempo remains the fallback.
The UF2
Mass Storage flasher is present as an experimental service/recovery workflow
and expects the controller to be manually placed into BOOTSEL/Mass Storage mode.
UF2 parsing, family validation and target-mismatch rejection are covered by
automated tests; a real write, disconnect and reboot has not yet been hardware-
tested on either RP2040 or RP2350 and must not be treated as confirmed.

For Firmware 4.0 sync bring-up, the Sync Setup Test card can configure the connected
USB NK4 controller as `NK-Master` or `NK-Follower`, set play mode `sync`, enable
sync and wireless, choose group 1-4 and select `long_range`, `balanced` or
`fast_sync`. Save is a separate visible action. Beacon diagnostics are shown for
USB inspection only. To test the Cardputer Beacon Master, configure existing
controllers as followers with `play_mode=sync`, `sync_enabled=1`,
`sync_role=follower`, matching `sync_group` 1-4, and `wireless_enabled=1`; then
start the Audio Beacon card with the same group, pattern, brightness and BPM.
Choose V1 for the established compatible path, V2 Manual to edit the five test
values, or one of the Mic modes for live analysis. The Mic controls expose
sensitivity, noise gate, smoothing, beat detect and pause. Capture uses
8 kHz mono frames with 256 samples (32 ms); Mic Full uses a small Goertzel
filter bank for roughly 60-250 Hz bass, 250-2000 Hz mids and 2000-3400 Hz
treble. Speaker UI sounds are suspended while the microphone is active because
the Cardputer cannot use both paths simultaneously. The serial monitor prints
periodic RMS, peak, noise floor, bands, confidence, beat timing and payload
diagnostics. V2 uses 29 of the 31 legacy advertising bytes and adds no local
name or service data.

The Cardputer catalog now covers all 27 controller patterns. The five new
entries are `audio_pulse_angle_color` (23), `audio_spectrum_ribbon` (24),
`audio_beat_ripples` (25), `audio_band_comets` (26), and `audio_beat_mosaic`
(27). V1 Manual, V2 Manual, and both microphone modes can advertise these IDs.
Firmware 4.0 pattern counts are detected from NK4; Firmware 3.x legacy
configuration remains limited to its 22 supported patterns.

Controller diagnostics for the two modes:

```text
NK4 seq=10 cmd=get section=sync
NK4 seq=20 cmd=audio_sync_status
```

Hardware check:

1. Configure the controller as a follower with sync and wireless enabled and a matching group.
2. Start `V2 Mic Full` on the Cardputer and select patterns 23, 24, 25, 26,
   and 27 in sequence while providing music or a stable pulse.
3. Run both diagnostic commands above over USB for each pattern.
4. Expect `sync_locked=1`, `local_pattern` or `sync_pattern` matching 23-27,
   `audio_valid=1`, `last_beacon_version=2`, rising `scan_decode_v2`, reactive
   energy/bands, plausible confidence and beat timing, and `scan_crc_fail=0`.

Firmware 4.0 diagnostics now include PatternClock and pattern classification
fields. The pattern list marks patterns as `S` sync-ready, `P` partial-sync, `L`
local/reactive, or `?` unknown. The Sync Diagnostics card shows compact sync runtime
values such as drift, phase, beacon phase, last beacon/applied sequence, apply
counts/skips, apply reason, pattern latency, and master-autoplay state. NightKite
The experimental BLE client reassembles 20-byte TX Notify chunks until newline
and supports one active BLE controller connection at a time. Multiple
simultaneous BLE connections are a future TODO. USB remains the recommended
service path.

The Tab5 UI uses a persistent navigation rail with `Connect`, `Control`,
`Patterns`, `Playback`, `Sync`, `Audio`, `Profiles`, `Controller`, `Service`
and `Firmware` pages. It supports complete profile CRUD/apply, pattern masks
and bulk edits, controller/sync/radio settings, manual and microphone Audio
Beacon modes, calibration, a guarded NK4 terminal, diagnostics, SD management
and target-checked RP2040/RP2350 UF2 flashing. Each settings area keeps its own
draft and reports persistence only after the complete apply-and-save sequence
succeeds; applying a profile remains deliberately live-only and requires confirmation.
Local sound, volume, touch-tone, startup-tone and display options use the same
persistent settings contract as Cardputer. Detailed target parity and deliberate
hardware differences are tracked in [the Cardputer/Tab5 function matrix](docs/TAB5_FUNCTION_MATRIX.md).
NK4 sequence
matching, initial refresh, queue ordering, timeouts and disconnect cleanup are
shared policy; target hardware and drawing remain isolated. The persistent
header shows Tab5 battery/charging state and the connected controller battery.
Serial diagnostics
are available as `status`, `reload`, `sd`, `audio`, `usb`, `gatt`, `beacon` and
`all`.

The P4 has no native Bluetooth radio. GATT and advertising require the onboard
C6 to run compatible ESP-Hosted SDIO coprocessor firmware. If the reported C6
version is `0.0.0`, Link stops before NimBLE initialization and keeps the UI
usable. M5Stack's factory recovery image restores Wi-Fi/SDIO only; it is a
recovery baseline, not a Hosted-BLE image. After recovery, install a
co-processor image matching the ESP-Hosted version pinned by the Tab5 build
through the same internal C6 download header. Link rejects versions older than
2.6 before starting NimBLE. Hardware validation used ESP-Hosted 2.12.11 and an
active-high C6-EN reset. The Beacon test transmits a valid V1 NightKite sync
packet for group 1; run it only with the intended follower controller nearby.
Advertising is refused while a GATT session owns the C6; starting a GATT scan
stops an active diagnostic advertiser. USB can remain active during advertising.
Tab5 USB host uses an 8192-byte receive buffer for full NK4 responses and retains
the library disconnect indication until the application has observed it.
On Tab5 the controller host path is the USB-A connector. The USB-C connector is
wired to the separate USB-device data pair with fixed device-role CC resistors
and cannot directly host a NightKite controller.
Hardware stability checks completed a 10:21-minute GATT session with eleven
full refreshes while the controller USB interface remained active, plus 16
Beacon windows with 16 parallel USB-host refreshes. The follower decoded 170
beacons without decode, CRC, or group failures.

The Controller card separates Link-side USB recovery from controller configuration:
`C reset USB` only restarts Link's USB/protocol session, while `F defaults`
opens a confirmation for controller factory defaults. Confirmed defaults send
`defaults confirm=1`, then `save`, then reload controller state. `S save` writes
the current live controller settings persistently.

Live changes such as brightness or active pattern are applied immediately but are
not persistent until `save` is sent. Firmware 4.0 persistent fields include
device name, brightness, active pattern, strip length, smoothing, accel/gyro
range, boot calibration, autoplay state/interval, play mode, boot mode,
enabled/inverted pattern masks, sync enabled/group/role/master/loss behavior,
and wireless enabled/profile. Runtime diagnostics such as PatternClock phase,
beacon counters, lock state, apply counts and battery readings are not
persistent settings.

## Links

- NightKite Multi: https://github.com/SunnyCodes86/nightkite-multi
- NightKite Link: https://github.com/SunnyCodes86/nightkite-link
- M5Cardputer Library: https://github.com/m5stack/M5Cardputer
- Cardputer-Adv Documentation: https://docs.m5stack.com/en/core/Cardputer-Adv
- Tab5 Documentation: https://docs.m5stack.com/en/core/Tab5
- Tab5 C6 Firmware Recovery: https://docs.m5stack.com/en/guide/restore_factory/m5tab5_c6_wifi
- ESP-Hosted: https://github.com/espressif/esp-hosted-mcu
- PlatformIO: https://platformio.org/
