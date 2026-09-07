# Show Control sender, SD player and USB bridge (V1)

NightKite Link is a runtime player/live controller/gateway. Create timelines on a
computer; Link renders no pixels and is not a small-screen show editor.

## Receiver and wire contract

The authoritative receiver specification is
[`nightkite-multi/docs/show-control-v1.md`](../../nightkite-multi/docs/show-control-v1.md).
Use a firmware build containing that implementation, with `wireless_enabled=1`
and `show_control enabled=1`, explicitly saved. Verify both after reboot. Group
matching for Audio V2 uses the receiver's `sync_group`, even for ALL/SINGLE show
commands. No connected GATT client during a broadcast show. An autonomous
controller master remains a separate operating mode and does not receive shows.

Show Control does not change Sync V1 or Audio/Sync V2. All three coexist on the
existing manufacturer company ID `FFFF`. A Show advertisement is exactly 31 bytes:

| Advertisement offsets | Bytes |
|---|---|
| 0–2 | `02 01 06` (Flags) |
| 3–6 | `1B FF FF FF` (manufacturer framing, company ID) |
| 7–30 | 24-byte Show V1 payload below |

| Payload offsets | Size | Meaning |
|---|---:|---|
| 0–1 | 2 | `4E 53`, ASCII NS |
| 2 | 1 | version 1 |
| 3 | 1 | target kind in bits 7–6; command in bits 5–0 |
| 4–6 | 3 | target value, LE |
| 7–8 | 2 | event ID, LE |
| 9–12 | 4 | current sender milliseconds, LE |
| 13–14 | 2 | low 16 bits of absolute execute time, LE |
| 15–21 | 7 | parameters; unused bytes zero |
| 22–23 | 2 | CRC16-CCITT, LE |

CRC polynomial `1021`, initial `FFFF`, no reflection/final XOR; calculate over
all 24 payload bytes with the CRC bytes zero. Golden vector (also in the Multi
host test):

```text
02 01 06 1B FF FF FF 4E 53 01 87 23 C1 AB 34 12 78 56 34 12 00 5A 2A 01 0F 0A 00 00 FF 73 85
```

This is SINGLE ABC123, SEGMENT image 42/index 1/start 15/count 10/blue,
event 1234 hex, sender 12345678 hex, execute 12345A00 hex.

| ID | File/API command | Parameters (decimal) |
|---:|---|---|
| 1 | PATTERN | pattern 1–27 |
| 2 | BRIGHTNESS | brightness 1–255 |
| 3 | SOLID | R G B brightness, each 0–255; brightness 0 retains/inherits |
| 4 | BLACKOUT | none |
| 5 | RELEASE | none |
| 6 | CLEAR | image ID 0–255, expected segment count 0–32 |
| 7 | SEGMENT | image ID, index 0–31, start, count, R, G, B |
| 8 | APPLY | image ID |
| 9 | CLOCK | generated internally: ALL, event 0, execute = sender, all parameters zero |

Target kind 0 ALL has value zero; kind 1 GROUP has value 1–255; kind 2 SINGLE
has a six-hex-digit existing controller short ID (0–FFFFFF). Kind 3 is reserved.
No sender identity, session field, master pinning, new pattern or wire type is added.

Brightness and output are runtime-only. Output commands acquire show authority;
BRIGHTNESS alone overrides brightness while leaving the underlying pattern
selection live. SOLID brightness zero retains an existing show brightness or
inherits the underlying brightness when acquiring output. Segment RGB is scaled
by that same runtime brightness. Battery caps/cutoffs have priority. RELEASE
restores the **current underlying** local/sync pattern and brightness and clears
the pending image. It never restores stale sync snapshots or writes configuration.
Patterns 23–27 still need fresh group-matching Audio V2; otherwise they are black.

## Shared implementation and timing

`ShowControl` is the portable Event/codec/Engine and fixed queue; `ShowInput` is
the strict shared command grammar, file preflight/player and USB request cache.
`ShowRuntime` binds Arduino FS/Stream, hardware RNG and the target's radio/audio.
Cardputer `ShowUi.inc` and Tab5 `ShowHardware.inc` provide hardware bindings.
Cardputer keys, SD playback and USB requests all enter the same Engine. Existing
beacon audio extraction/DSP is reused; no input path has its own BLE encoder.

The clock is uptime `millis()`, modulo 2^32. ARM chooses `esp_random()`'s low 16
bits as the first event ID. Every accepted event (including STOP's RELEASE) advances
it modulo 65536. ARM does not reset time. A new boot does not resume a show. Stop
any previous sender before arming its replacement. The receiver has no sender pin.

State progression: OFF → ARMING → READY → PLAYING (SD) → READY at EOF.
ARMING requires at least 3000 ms and 15 successful fresh CLOCK publications;
a CLOCK gap over 300 ms restarts that warmup. Successful radio API calls cannot
prove reception. Re-ARM while active is idempotent. CLOCK continues throughout
READY/PLAYING/STOPPING; receiver clock adjustment does not move frozen events.

The receiver reconstructs `sender + signed16(executeLow16 - low16(sender))`.
Wire range is -250…30000 ms. Link publishes only inside **1200 ms lookahead**,
well below the ambiguous half-range. All internal comparisons are wrap-safe.
Local accepted deadlines are 800 ms…24 hours ahead. Live/NOW defaults to **1000 ms**
lead; IN explicitly selects a lead within those bounds. AT accepts the absolute
32-bit Link clock; old/too-near times are rejected. Two SINGLE events can use
one AT value and different colors. Use ALL/GROUP where possible.

The local queue has 32 entries, never evicts, and holds a host's short timeline.
SD retains one pending event while streaming. A conservative admission limit
allows at most three nearby events within 1200 ms, regardless of target; dense
or out-of-order host bursts may be rejected conservatively. Files require at
most three events in each half-open 1200-ms interval. At most six transmitted
unexpired events are counted against the receiver's eight slots (two reserved
for drain/release margin). Large fleet fan-outs need groups or more time; this
version deliberately rejects excessive density rather than silently losing events.

The central arbiter chooses one fresh payload every ≥40 ms. Audio V2 has first
priority when ≥120 ms old; CLOCK follows at ≥200 ms; remaining slots carry events.
Nominal simulation with continuous audio reaches 120-ms audio / ≤240-ms CLOCK gaps.
Each event gets three publications separated by ≥80 ms, ending ≥40 ms before due.
Every publication keeps ID, target, parameters and absolute execute time; only
sender time and CRC refresh. The BLE controller may also repeat each payload.
Advertising uses nonconnectable legacy packets at 20–25-ms configured intervals;
actual dwell/air reception depends on the BLE driver and RF conditions.

Link never intentionally transmits a late event. A missed publication budget is
counted (`missed`); receiver lateness policy remains next-loop execution up to
250 ms late, then drop. Receiver dedupe is equality over 64 recent IDs plus queued
IDs, not a global numeric high-water mark. Link never restarts retries of retired
events. Random start minimizes, but cannot eliminate, a replacement sender's ID
collision with that cache. Counters measure successful payload installation,
**not acknowledged packets on air**. STATUS exposes counts, max gaps, last ID,
last publication time, last deadline, queue depth, rejected/missed/radio errors.

### STOP, DISARM and EOF

STOP immediately stops local playback, discards locally queued events and stops
retransmitting them. Already transmitted receiver events **cannot be cancelled**.
Link schedules ALL RELEASE after both `now + 1000 ms` and the latest transmitted
deadline + 100 ms. It remains STOPPING, continues audio/CLOCK and repeats RELEASE;
after deadline + 250 ms it returns READY. DISARM uses the same drain then turns
advertising OFF. Repeated STOP/DISARM does not postpone the drain. Failure to
publish RELEASE even once ends in ERROR, not a claimed successful stop. With no
receiver acknowledgments a successful send still cannot guarantee reception.

There is no pause/resume or automatic playback after reboot. Use Stop/Restart.
EOF leaves the last intended output in place and returns READY; put RELEASE (or
BLACKOUT) at the end of a file if required. Explicit RELEASE is an ordinary event,
not queue cancellation; use STOP to drain a timeline safely.

## SD file format

Files are `/shows/<name>.nks`. Names: 1–36 ASCII letters/digits/underscore/hyphen
plus `.nks` (40 characters total). Case-sensitive commands. ASCII lines, LF or
CRLF; at most 223 bytes before LF. `#` starts a comment; empty lines are ignored.
Tabs/spaces separate tokens. No scripts, includes, loops, implicit frames or heap
storage proportional to timeline length.

```text
NKSHOW 1
NAME Demo Show
# milliseconds from start; nondecreasing, maximum 86400000
0 ALL PATTERN 6
2000 SINGLE A1B2C3 SOLID 255 0 0 255
2000 SINGLE D4E5F6 SOLID 0 0 255 255
4000 ALL SOLID 255 255 255 255
4200 ALL PATTERN 12
6000 SINGLE A1B2C3 PATTERN 23
6000 SINGLE D4E5F6 PATTERN 25
8000 ALL BLACKOUT
8500 ALL RELEASE
```

First non-comment line must be `NKSHOW 1`. Optional `NAME` appears once before
events, 1–40 printable characters. Each event line is `milliseconds TARGET COMMAND
parameters`, using the table above. Time must not decrease (equal times allowed)
and the radio density limit applies. At least one event is required. LOAD opens
only while OFF and validates the entire file incrementally (≤512 input bytes per
loop) before LOADED. PLAY requires READY and an empty local queue, rewinds, starts
at current clock + 1000 ms, and streams only the next lookahead events. PLAY after
END or STOP restarts from the beginning. Unexpected I/O/parser errors during play
stop admission and initiate the same controlled RELEASE drain; error/line remain
visible. Do not edit/remove a file during playback; no immutable file snapshot is
held in RAM. SD scanning/upload/loading is done before ARM.

Segments use the receiver's **logical strip 1 followed by logical strip 2**:
`0..L-1`, then `L..2L-1`, for configured strip length L (10–35). Physical reversal
is handled by the receiver. `count > 0`, `start + count <= 70`; the sender cannot
infer every target's L, so the receiver also rejects beyond its actual 2L.
CLEAR starts a black pending image; index must be unique and below the declared
count. All expected indices must arrive for APPLY to switch atomically. Zero
expected segments is a valid all-black image. Missing segments leave visible
output unchanged. RGB overlaps between different indices are allowed: later
scheduled segment wins. Use distinct increasing preparation times for dependent
CLEAR/SEGMENT/APPLY operations. Example:

```text
NKSHOW 1
0 ALL CLEAR 7 2
400 ALL SEGMENT 7 0 0 8 255 0 0
800 ALL SEGMENT 7 1 15 10 0 0 255
1600 ALL APPLY 7
4000 ALL RELEASE
```

File preflight permits one pending image transaction at a time, requires matching
target/image ID, rejects duplicate/missing indices, CLEAR before completion and
RELEASE before APPLY. Image IDs may be reused after a completed transaction. These
are strict file-safety rules on top of the receiver contract. Raw USB transactions
use the same individual Event validation; the host owns transaction completeness,
ordering and target lengths, and can observe rejects via receiver USB diagnostics.

## USB Host API V1

115200 baud, newline terminated ASCII; maximum request length 223 bytes. Responses
are one line, under 512 bytes. Send `NKSHOW 1 <request-id> <operation>`. IDs are
unsigned decimal 32-bit; initialize a host session randomly, then increment modulo
2^32. One outstanding request is simplest. Await a reply; on timeout retransmit
**the identical line with the same ID**. The last 16 requests retain exact replies,
including original time/deadline/ID. Conflicting reuse returns `request_conflict`.
Do not retry after 16 newer requests or across a Link reboot; query state and
re-establish the session instead. Cached ERROR replies stay errors; correct/retry
with a new request ID. This is bounded idempotence, not permanent replay protection.

| Operation | Effect |
|---|---|
| HELLO / VERSION | API/wire version, default lead and lookahead |
| ARM / DISARM | Warm clock / controlled drain and OFF |
| STATUS / TIME | State and Link uptime; STATUS adds diagnostics/player information |
| EVENT NOW TARGET COMMAND parameters | Current clock + default lead |
| EVENT IN delay TARGET COMMAND parameters | Relative deadline in ms |
| EVENT AT milliseconds TARGET COMMAND parameters | Absolute 32-bit Link clock deadline |
| AUDIO OFF / MANUAL / MIC_ENERGY / MIC_FULL | Select existing V2 source; OFF disables V2, not CLOCK/show |
| LIST | `files=` comma-separated `.nks` basenames, bounded directory scan, OFF only |
| LOAD filename.nks | Start full preflight; poll STATUS until LOADED or ERROR, OFF only |
| PLAY / STOP | Start loaded file / stop admission and drain to RELEASE |
| PUT_BEGIN filename.nks | OFF-only upload to `.part`, reject existing final file |
| PUT_LINE file-line | Validate and append one line (including header/comment) |
| PUT_END | Validate completeness, flush/close and rename to final path |

PUT is optional convenience for creating a test show on the inserted SD via USB;
no mass-storage mode is needed. Uploads are capped at 4 MiB, one transaction, no
replacement of existing final files. Invalid content/failed writes remove the
partial file. A reboot can leave `.part`; a new PUT_BEGIN of that filename removes
it. ARM refuses an active upload. LOAD independently prevalidates saved contents
before execution. File list is capped at 64 directory entries and response length;
large libraries should use an already known basename with LOAD.

```text
NKSHOW 1 100 HELLO
NKSHOW 1 100 OK state=OFF time=5040 api=1 wire=1 lead_ms=1000 lookahead_ms=1200
NKSHOW 1 101 ARM
NKSHOW 1 101 OK state=ARMING time=5100
# Poll with NEW request IDs, until state=READY (at least 3 s).
NKSHOW 1 102 STATUS
NKSHOW 1 103 EVENT NOW ALL PATTERN 6
NKSHOW 1 103 OK state=READY time=9200 accepted=1 event_id=65000 execute_at=10200
NKSHOW 1 104 EVENT AT 12000 SINGLE A1B2C3 SOLID 255 0 0 255
NKSHOW 1 105 EVENT AT 12000 SINGLE D4E5F6 SOLID 0 0 255 255
NKSHOW 1 106 EVENT IN 1000 ALL BLACKOUT
NKSHOW 1 107 STOP
```

Example error: `NKSHOW 1 108 ERROR code=not_ready state=ARMING time=5500`.
Errors: `version`, `request_id`, `command`, `target`, `parameters`, `pattern`,
`brightness`, `segments`, `segment`, `time`, `line_length`, `request_conflict`,
`reserved`, `not_ready`, `lead`, `queue_full`, `density`, `player_busy`, `busy`,
`radio_busy`, `audio_mode`, `audio_busy`, `mic`, `unsupported`, `filename`, `sd`,
`file`, `io`, `exists`, `no_upload`, `not_loaded`, `queue_busy`. File errors also
include `header`, `empty`, `name`, `time_order`, `image`, `segment_index`,
`incomplete_image`, `character`, `sender_stopped`. Oversized/non-ASCII transport
lines are discarded through newline and reply with request ID 0 / `line_length`;
subsequent valid requests recover normally. Prefix-filter `NKSHOW 1 ` replies on
Tab5 because its existing diagnostic console also uses Serial.

See [`tools/show_bridge_example.py`](../tools/show_bridge_example.py) for a bounded,
standard-library-only POSIX (macOS/Linux) demo. Windows software can use any serial
library with this same text API; no BLE implementation is needed on the PC.

## Cardputer controls and USB roles

The existing carousel gains one Show card. `C` cycles fields; `+/-` changes values;
Enter applies. Fields: ARM/DISARM, target kind, group/controller, pattern,
brightness, solid color, SD file, PLAY, STOP, USB role. `F` scans `/shows` while
OFF; select/load a file before ARM, then wait LOADED/READY and PLAY. Status shows
sender/player state, show time, last event ID, local queue, next deadline and bridge.
Quick keys: `A` arm/disarm, `W` static white, `B` blackout, `R` targeted RELEASE,
`X` STOP/ALL RELEASE. White stays until another command; this is not an automatic
flash pulse. SINGLE selection reuses the existing BLE `NK-xxxxxx` scan results or
the connected USB controller's short ID/name. Scan/read identities before ARM.
Audio mode/group/levels use the existing Audio Beacon settings or USB AUDIO mode.

Cardputer's one USB controller cannot be both PC device and controller host.
**PC Bridge mode requires USB to be connected before Cardputer boot.**
Save Bridge while disarmed, switch off, connect USB to the PC, then switch on.
Starting on battery and attaching USB later currently does not reliably enumerate;
no periodic USB restart/recovery is attempted. The UI/player can run on battery.

Select the saved mode explicitly in Show's USB role field while disarmed. Enter
toggles the next-boot preference and checks that it was saved. The current role
stays fixed until a manual restart; unplug the PC before starting Controller Host.
Default and migrated V1 Link settings use Controller Host. PC Bridge starts only
native USB CDC (USB Serial/JTAG); it never starts the USB host/CDC-ACM/MSC drivers
and never waits for an opened PC serial port. Host mode never initializes the
bridge device driver. No automatic cable detection or runtime PHY switching is used.
The pinned Arduino-ESP32 3.2.1 / ESP-IDF 5.4.2 Cardputer build keeps `ARDUINO_USB_MODE=1`
(hardware Serial/JTAG CDC) and `ARDUINO_USB_CDC_ON_BOOT=0`. Consequently `Serial`
is `HardwareSerial Serial0` (UART0), keeping existing diagnostics off the bridge.
A separate `HWCDC bridgeSerial` is compiled and started explicitly only in Bridge.
`CDC_ON_BOOT=0` does not exclude that driver. TinyUSB/USBCDC is not used here.
Before `HWCDC::begin`, the ESP-IDF HAL enables the Serial/JTAG bus clock and
explicitly maps the shared internal PHY
(GPIO19 D− / GPIO20 D+) to Serial/JTAG. Arduino's `HWCDC::begin` alone does not
set the RTC PHY multiplexer; an OTG selection can otherwise leave CDC invisible.
Controller Host instead lets the existing USBHostSerial `usb_host_install` path
select that same PHY for OTG Host. Neither role initializes the other driver.
The resulting Bridge VID/PID is Espressif `303A:1001`; normal reboot needs no G0.
The [official M5 example](https://docs.m5stack.com/en/core/Cardputer) uses
`CDC_ON_BOOT=1` to alias its `Serial` to HWCDC; Link deliberately uses separate
UART diagnostics and runtime-selected HWCDC instead. No second firmware is needed.

The existing 9-byte Link settings record is version 2: bit 3 of flags stores the
Bridge preference, with unchanged other fields/checksum. V1 records remain readable
and migrate to Host without changing sound/display settings. This preference alone
is persistent; a reboot never resumes a show. Tab5 retains separate USB-A controller host and USB-C device console;
it gets the shared API/player/sender, without an added touch show editor/page.

Radio and queue failures are surfaced; there is no acknowledgment channel,
queue-cancel, optical synchronization proof, pause/resume, frame streaming,
MIDI/OSC, desktop editor or new persistent show state in this version.
