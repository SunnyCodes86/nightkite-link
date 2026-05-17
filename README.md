# NightKite Link

NightKite Link is an open-source handheld configurator and service device for
[NightKite Multi](https://github.com/SunnyCodes86/nightkite-multi). It runs on an
M5Stack Cardputer-Adv / StampS3 target and uses USB host communication to talk to
NightKite Multi controllers. Firmware 4.0 controllers are detected with USB NK4
first; older Firmware 3.x controllers fall back to the existing USB legacy CLI.
The current UI is card-based and optimized for the small 240 x 135 px display.

Full English documentation: [docs/README.en.md](docs/README.en.md)

NightKite Link ist ein Open-Source-Handheld zum Konfigurieren und Warten von
[NightKite Multi](https://github.com/SunnyCodes86/nightkite-multi). Es läuft auf
einem M5Stack Cardputer-Adv / StampS3 und nutzt USB-Host-Kommunikation zu
NightKite-Multi-Controllern. Firmware-4.0-Controller werden zuerst per USB-NK4
erkannt; ältere Firmware-3.x-Controller fallen auf die bestehende USB-Legacy-CLI
zurück. Die aktuelle Oberfläche ist kartenbasiert und auf das kleine 240 x 135 px
Display ausgelegt.

Ausführliche deutsche Dokumentation: [docs/README.de.md](docs/README.de.md)

## Features

- Card-based UI with compact status bar
- WAV/PCM-style startup and UI key sounds
- Cardputer battery, USB and controller/CLI status display
- USB NK4 detection for Firmware 4.0 with legacy CLI fallback for Firmware 3.x
- Device, play mode, sync and wireless/beacon diagnostic cards
- Sync Test card for preparing master/follower two-controller beacon tests
- Brightness, strip length, active pattern, smoothing and autoplay settings
- Pattern list with cycle and invert state
- Pattern detail and bulk actions
- SD card profiles under `/profiles/`
- UF2 firmware selection from `/firmware/`
- Experimental USB Mass Storage UF2 flasher workflow for RP2040/RP2350 BOOTSEL mode

## Hardware Target

- M5Stack Cardputer-Adv / StampS3
- ESP32-S3
- 1.14" 240 x 135 px display
- Cardputer keyboard
- microSD card
- USB-OTG connection to a NightKite Multi controller

## Build

PlatformIO environment: `cardputer`

```sh
pio run
pio run -t upload
pio device monitor
```

## Current Status

The project is functional but still evolving. The USB CLI configuration workflow
and card UI remain the stable operating path. USB NK4 support adds Firmware 4.0
identity, play mode, sync and wireless configuration without requiring BLE. The
BLE NK4 firmware transport exists on compatible controllers, but NightKite Link
does not include a BLE client yet. Link configures sync; it is not a real-time
sync relay. The UF2 Mass Storage flasher is present as an experimental
service/recovery workflow and expects the controller to be manually placed into
BOOTSEL/Mass Storage mode.

For Firmware 4.0 sync bring-up, the Sync Test card can configure the connected
USB NK4 controller as `NK-Master` or `NK-Follower`, set play mode `sync`, enable
sync and wireless, choose group 1-4 and select `long_range`, `balanced` or
`fast_sync`. Save is a separate visible action. Beacon diagnostics are shown for
USB inspection only; Link does not relay real-time sync traffic.

## Links

- NightKite Multi: https://github.com/SunnyCodes86/nightkite-multi
- NightKite Link: https://github.com/SunnyCodes86/nightkite-link
- M5Cardputer Library: https://github.com/m5stack/M5Cardputer
- Cardputer-Adv Documentation: https://docs.m5stack.com/en/core/Cardputer-Adv
- PlatformIO: https://platformio.org/
