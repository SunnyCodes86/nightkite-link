# NightKite Link

NightKite Link is an open-source handheld configurator and service device for
[NightKite Multi](https://github.com/SunnyCodes86/nightkite-multi). It runs on an
M5Stack Cardputer-Adv / StampS3 target and uses USB host communication to talk to
the existing NightKite USB CLI. The current UI is card-based and optimized for
the small 240 x 135 px display.

Full English documentation: [docs/README.en.md](docs/README.en.md)

NightKite Link ist ein Open-Source-Handheld zum Konfigurieren und Warten von
[NightKite Multi](https://github.com/SunnyCodes86/nightkite-multi). Es laeuft auf
einem M5Stack Cardputer-Adv / StampS3 und nutzt USB-Host-Kommunikation zur
vorhandenen NightKite-USB-CLI. Die aktuelle Oberflaeche ist kartenbasiert und auf
das kleine 240 x 135 px Display ausgelegt.

Ausfuehrliche deutsche Dokumentation: [docs/README.de.md](docs/README.de.md)

## Features

- Card-based UI with compact status bar
- Cardputer battery, USB and controller/CLI status display
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
and card UI are the main operating path. The UF2 Mass Storage flasher is present
as an experimental service/recovery workflow and expects the controller to be
manually placed into BOOTSEL/Mass Storage mode.

## Links

- NightKite Multi: https://github.com/SunnyCodes86/nightkite-multi
- NightKite Link: https://github.com/SunnyCodes86/nightkite-link
- M5Cardputer Library: https://github.com/m5stack/M5Cardputer
- Cardputer-Adv Documentation: https://docs.m5stack.com/en/core/Cardputer-Adv
- PlatformIO: https://platformio.org/
