# NightKite Link

NightKite Link is a compact handheld configurator and service device for
NightKite Multi. The target hardware is an M5Stack Cardputer.

The first firmware version is intentionally scoped as a serial terminal and
macro controller for the existing NightKite Multi USB CLI. It does not require
changes to the NightKite Multi firmware.

## Current Status

- M5Stack Cardputer PlatformIO project
- Cardputer UI skeleton with Status, Patterns, Config, Service, and Terminal views
- Parser for NightKite CLI `OK key=value` and `ERR ...` replies
- Debug serial transport for UI and parser development
- Transport abstraction prepared for a later USB-CDC host implementation

## Controls

- `Tab` or `A`/`D`: switch view
- `W`/`S`: move selection
- `Enter`: run selected command
- `Backspace`: back or cancel
- `1`: `show`
- `2`: `battery`
- `3`: `sensor`
- `4`: `patterns`
- `5`: `timing`
- `6`: `offsets`

## Build

```sh
pio run
```

## Target

- M5Stack Cardputer / StampS3
- Arduino framework
- PlatformIO
