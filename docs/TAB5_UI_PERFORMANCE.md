# Tab5 UI performance

Measured on the connected Tab5 (ESP32-P4 rev. 1.3) at 1280 x 720, RGB565,
using the pinned M5GFX/M5Unified versions. Values are arithmetic means from the
serial `ui-perf` benchmark; they are not inferred from build times.

## Root cause and renderer

The former dirty-region implementation clipped only the MIPI-DSI transfer.
Every update still cleared and repainted the complete 1280 x 720 `M5Canvas`.
The retained renderer now keeps the canvas contents and:

- repaints the full scene only for page or modal transitions;
- clears and redraws only invalidated widgets for ordinary updates;
- merges overlapping regions and regions separated by at most 16 pixels;
- promotes combined dirty areas of 45% or more to one full refresh;
- transfers all remaining regions inside one display transaction;
- limits status/queue coalescing to 20 Hz, audio live values to 4 Hz, firmware
  progress to 10 Hz, and battery updates to 1 Hz and only on change.

Touch-down paints immediate feedback; the action remains release-triggered.
The first feedback frame runs before UI tones, diagnostics, protocol polling,
or storage work. Profile/firmware lists are cached and scanned incrementally;
UF2 validation is blockwise. Profile and SD operations are queued out of the
touch handler.

## Retained renderer A/B

Pioarduino 55.03.37, standard project Kconfig:

| Path | Canvas | Display transfer | Dirty area | Transfers | Frame completion |
| --- | ---: | ---: | ---: | ---: | ---: |
| Former full repaint | 81.652 ms | 654.040 ms | 921,600 px (100%) | 1/frame | 735.692 ms |
| Retained regional | 8.022 ms | 7.265 ms | 61,355 px (6.65%) | 1/frame | 15.287 ms |
| Retained regional, native layout | 6.458 ms | 7.202 ms | 61,355 px (6.65%) | 1/frame | 13.660 ms |

The controlled native regional workload is about 53.9 times faster end-to-end
than the former full repaint. Its touch-to-pixel counter starts at physical
touch-down and ends after the feedback region has been transferred. Rotation-3
samples averaged 1.434 ms with a 1.551-ms maximum; a native-layout sample
measured 1.442 ms.

## Toolchain A/B

The source, benchmark workload, Kconfig, display and device were identical:

| Pioarduino | Arduino / IDF | Canvas | Transfer | Rotation 3 transfer |
| --- | --- | ---: | ---: | ---: |
| 55.03.35 | 3.3.5 / 5.5.1 | 8.089 ms | 7.388 ms | 382.204 ms |
| 55.03.37 | 3.3.7 / 5.5.2 | 8.022 ms | 7.265 ms | 367.171 ms |

55.03.37 is slightly faster for this workload and remains pinned. The BLE
initialization call was made source-compatible with both return signatures;
the production Hosted-BLE state check remains unchanged.

## Kconfig

The Espressif-recommended performance combination was built with `-O2`,
256-KB L2 cache, 128-byte cache lines and PSRAM XIP. It did not reach the
application: IDF 5.5.2 repeatedly asserted in `image_process.c` while processing
the XIP image segments. None of these experimental settings is retained; the
stable project Kconfig remains `-Og`, 128-KB L2 and 64-byte cache lines without
PSRAM XIP.

## Native display layout

The production display now stays at its physical rotation 0 (720 x 1280). The
single active RGB565 `M5Canvas` is explicitly allocated in PSRAM as a physical
720 x 1280 sprite and uses canvas rotation 3 to preserve the logical 1280 x 720
UI. M5GFX therefore stores the pixels in native panel order and `pushSprite()`
can use the non-rotating transfer path. There is no raw framebuffer copy.

All transforms are centralized in `Tab5WorkflowPolicy.h`:

- native touch `(x, y)` becomes logical `(1279 - y, x)`;
- logical point `(x, y)` becomes native `(y, 1279 - x)`;
- logical rectangle `(x, y, w, h)` becomes native `(y, 1280 - x - w, h, w)`.

Host checks cover all corners, center, full dimensions, modal and keyboard
rectangles. The hardware touch benchmark covered three corners and center; the
logical coordinates matched the displayed orientation.

The isolated transfer comparison used identical RGB565 content:

| Operation | Rotation 3 display | Native rotation 0 | Result |
| --- | ---: | ---: | ---: |
| Paint complete template | 72.768 ms | 74.195 ms | native canvas +2.0% |
| Full 1280 x 720 transfer | 652.790 ms | 93.508 ms | native 6.98x faster |
| 955 x 102 regional transfer | 11.528 ms | 11.779 ms | effectively unchanged |

Seven sampled pixels, including corners and center, matched exactly. An
interior-panel fast clear offsets the small rotated-canvas drawing cost for
ordinary retained updates; the controlled regional frame improved from 15.287
to 13.660 ms.

## PSRAM template-cache evaluation

The active canvas occupies 1,843,200 bytes. At boot the device reported
29,839,388 bytes free PSRAM and a 29,360,128-byte largest block after allocating
that canvas. Every benchmark sprite used `setPsram(true)`, verified its buffer
with `esp_ptr_external_ram()`, and reported free and largest blocks. Allocation
failure skips the cache and leaves the normal renderer intact.

Full native-layout page templates produced these measurements while the RGB565
reference and the candidate coexisted. Mismatch and channel-error samples were
zero after explicitly configuring the indexed palette, so theme colors and
rendered text were unchanged in the sampled image.

| Format | Payload | Generation | Copy to RGB565 canvas | Free PSRAM | Largest block |
| --- | ---: | ---: | ---: | ---: | ---: |
| RGB565 | 1,843,200 B | 102.977 ms | 80.030 ms | 16,764,908 B | 16,515,072 B |
| 8-bit palette | 921,600 B | 58.940 ms | 136.551 ms | 17,698,796 B | 17,301,504 B |
| 4-bit palette | 460,800 B | 32.761 ms | 132.132 ms | 18,165,740 B | 17,825,792 B |

An idle-prewarm prototype generated the start page first and the remaining
pages one at a time after 500 ms without input. Ten 8-bit templates consumed
about 9.34 MB and still left about 20.50 MB free with a 20.45 MB largest block.
It was removed from the production path because copying an indexed template is
slower than repainting the static content.

A smaller shared navigation cache, with the selected item redrawn dynamically,
confirmed the same result:

| Navigation path | Payload | Generation | Render/copy plus selection |
| --- | ---: | ---: | ---: |
| Direct retained drawing | 0 B | n/a | 13.126 ms |
| RGB565 sprite | 274,500 B | 13.105 ms | 13.445 ms |
| 8-bit sprite | 137,250 B | 6.777 ms | 22.092 ms |
| 4-bit sprite | 68,625 B | 5.060 ms | 21.336 ms |

Consequently the selected cache layout is deliberately minimal: one active
RGB565 PSRAM canvas and no persistent page/header/navigation/widget cache.
Per-page templates, shared chrome, button/icon sprites, modals and keyboard
sprites were rejected because their copy/conversion cost cannot beat the
already retained direct drawing. There is therefore no LRU or production
prewarm code to maintain.

## Page-switch A/B

| Layout | Template | Render | Cache copy | Display | Complete page switch |
| --- | --- | ---: | ---: | ---: | ---: |
| Rotation 3 | none | 92.286 ms | 0 | 661.561 ms | 753.937 ms |
| Rotation 3 | 8-bit native template | 21.494 ms | 135.972 ms | 660.539 ms | 818.327 ms |
| Native rotation 0 | none | 96.773 ms | 0 | 94.110 ms | 191.961 ms |
| Native rotation 0 | 8-bit native template | 26.453 ms | 136.563 ms | 94.061 ms | 258.251 ms |

The adopted no-cache native path makes a complete page switch 3.93 times faster
than the old rotation-3 path. A template saves about 70 ms of drawing but adds
about 136 ms of conversion/copying, so it is a net loss in either orientation.
RGB565 copies faster but still does not beat direct drawing and doubles the
memory per page. Indexed formats are therefore useful only as a future option
if M5GFX gains a materially faster indexed-to-RGB565 blit.

## Diagnostics and future direction

`NIGHTKITE_TAB5_UI_PERF=1` enables canvas time, display-transfer time, dirty
pixels, transfer count, touch-to-pixel latency, legacy/regional comparison,
cache formats, PSRAM state and rotation commands. Normal builds default it to
`0`, compiling the counters and commands out. The native display layout is the
only production display path.

No PPA path or additional UI task was added: neither addresses the invalidation
root cause, and the retained path already completes the controlled widget update
in about 14 ms. The display and canvas remain in one UI context.

The current M5Canvas renderer should be retained. A production LVGL migration is
not justified by these measurements. If future animated or full-screen pages
remain materially slower than the official Tab5 demo, the next step is a small
separate `esp_lvgl_adapter` prototype using MIPI DSI `DOUBLE_DIRECT`; Espressif
documents that mode for small widget deltas and requires locked LVGL access.
That prototype should compare the same screen and touch workload before any UI
migration.

## Primary references

- [Espressif ESP LVGL Adapter](https://docs.espressif.com/projects/esp-iot-solution/en/latest/display/tools/esp_lvgl_adapter.html)
- [ESP-IDF heap capabilities and PSRAM metrics](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32p4/api-reference/system/mem_alloc.html)
- [ESP-IDF MIPI DSI LCD driver](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/lcd/dsi_lcd.html)
- [M5Stack Tab5 UserDemo](https://github.com/m5stack/M5Tab5-UserDemo)
- [M5Stack Tab5 UserDemo documentation](https://docs.m5stack.com/en/esp_idf/m5tab5/userdemo)
- [M5GFX framebuffer implementation](https://github.com/m5stack/M5GFX/blob/master/src/lgfx/v1/panel/Panel_FrameBufferBase.cpp)
- [Pioarduino 55.03.35](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.35)
- [Pioarduino 55.03.37](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.37)
