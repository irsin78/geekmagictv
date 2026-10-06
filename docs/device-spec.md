# GeekMagic SmallTV (ESP8266): measured specs and limits

Measured on one device (stock firmware V3.1.4) with the `safeboot` diagnostic firmware
(0.2.x at 80 MHz, 0.3.0 at 160 MHz). Re-run the measurements with `tools/probe.py`.

## Hardware

| Item | Value | Source |
|---|---|---|
| MCU | ESP8266 (ESP-12F module), single core, no FPU | measured |
| Clock | 80 MHz default; 160 MHz works as a **build option** (`board_build.f_cpu`); switching at runtime has no effect | measured |
| Flash | 4 MB **Puya** (JEDEC ID `0x162085`), DIO, 40 MHz | measured |
| Display | ST7789, 240×240, RGB order, SPI MODE3, no CS line, clean up to 80 MHz SPI | measured + visual check |
| Pins | SCLK 14, MOSI 13, DC 0, RST 2, backlight GPIO5 (active low, PWM) | disassembled stock firmware |
| Input | **none** (no button, no touch): everything is controlled over the network | visual check |
| Radio | 2.4 GHz 802.11 b/g/n, no Bluetooth | — |
| USB | **power only**: no USB-serial chip, nothing enumerates on a computer | measured |

Notes:

- **Display init:** TFT_eSPI's generic ST7789 init leaves this panel **black** (wrong VGH/VCOM/VRH and
  gamma for this panel). Call `tft.init()` and then replay the stock firmware's init sequence; see
  `panelInitStock()` in `firmware/usagebar/src/display.cpp`. The sequence, SPI settings and pins were
  recovered by disassembling the stock image.
- **Display init speed:** initialising at 160 MHz CPU / 80 MHz SPI also gave a black screen; 80 MHz CPU
  with 40 MHz SPI is the verified combination (raising SPI to 80 MHz *after* init works).
- **Power:** some USB-C cables / USB-C host ports don't power the device (the board likely lacks the
  CC resistors). A USB-A charger with an A-to-C cable works.

## Flash layout (4M3M, same as stock)

| Region | Range | Notes |
|---|---|---|
| Application + OTA staging | 0x000000–0x100000 (1 MB) | **current image + new image must fit in about 1 MB** |
| LittleFS | 0x100000–0x3FA000 (2.98 MB, 8 KB blocks) | stock web UI, GIFs, fonts, settings (1.36 MB used) |
| EEPROM | 0x3FB000 | unused by stock; `usagebar` keeps its settings here |
| SDK config | 0x3FC000–0x400000 | RF calibration, Wi-Fi config |

- Keep application images **under about 500 KB** so OTA works in both directions.
- `ESP8266HTTPUpdateServer` accepts at most `getFreeSketchSpace() - 4096` bytes. From a large
  (550 KB) image the stock image (492,656 bytes) did not fit; downgrading needed a smaller hop first.
- Size references: Wi-Fi + web + OTA ≈ 346 KB; + display/JPEG/diagnostics ≈ 470 KB;
  HTTPS (BearSSL) adds ≈ 80–100 KB.
- Never mount or format LittleFS from custom firmware if you want to keep the stock data, and never
  use an update form's "filesystem" upload (it erases the stock LittleFS).

## Memory

- About **34–42 KB** of free heap with Wi-Fi and the web server running; largest block about the same.
- Fits: a 240×60 16-bit strip (28.8 KB), a full-screen 1-bit buffer (7.2 KB).
- Does not fit: a full-screen 8-bit (57.6 KB) or 16-bit (115 KB) frame buffer.

## Performance

| Item | 80 MHz | 160 MHz |
|---|---|---|
| Integer (200k xorshift) | 35 ms | **17.5 ms** |
| Float (20k multiply-add, soft float) | 36 ms | 18 ms |
| memcpy | 93 MB/s | 186 MB/s |
| Full-screen fill (SPI 40 / 80 MHz) | 24 / 12 ms | 24 / 12 ms |
| Full frame from RAM | 29 ms | 27 ms |
| 50 lines of text (16 px) | 163 ms | 121 ms |
| JPEG 240×240 decode only | 116 ms | **58 ms** |
| JPEG decode + draw | 147 ms (~7 fps) | **94 ms (~10 fps)** |
| Download (device→PC) | 228 KB/s | 266 KB/s |
| Upload (PC→device) | 205 KB/s | 320 KB/s |
| PC→display RAW frame | 600 ms (1.7 fps) | 390 ms (2.6 fps) |
| PC→display JPEG frame (~3 KB) | 170 ms (6 fps) | 118 ms (8.5 fps) |

Wi-Fi during the measurements: −70 dBm, channel 11, 802.11g. A better signal raises the network figures.

## HTTPS (160 MHz, BearSSL, no certificate validation; measured with an earlier safeboot build)

| Host | Handshake | Free heap while connected (default / small buffers) |
|---|---|---|
| www.google.com | 0.6 s | 6.8 KB / 19 KB |
| api.openweathermap.org | 1.4 s | 6.8 KB / 19 KB |
| api.github.com | 0.5 s | 6.8 KB / 19 KB |

- Default buffers (16 KB) leave only ~7 KB of heap: **small buffers (4 KB) are required**.
- Heap comes back after disconnecting; one connection at a time is realistic.
- The biggest cost is code size (+80–100 KB).

## Conclusions

- **Works well:** text/icon dashboards, clocks, notifications, JPEG images and slideshows (~10 fps),
  partially updated animations, a computer rendering images and pushing them (~8 fps).
- **Possible with care:** calling HTTPS APIs from the device (small buffers, one at a time, watch the
  code size budget).
- **Not realistic:** video, full-frame-buffer UIs, LVGL, any local input, Bluetooth.
