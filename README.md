**English** | [한국어](README.ko.md)

# SmallTV AI usage display

Custom firmware that turns a [**GeekMagic SmallTV**](https://github.com/GeekMagicClock/smalltv) (ESP8266, 240×240 display) into a desk display
for AI coding-assistant quotas: **Claude Code, Codex and Antigravity**. It shows how much of each
limit is left, a countdown to the next reset, a clock and the weather, with little animated mascots.

<img src="docs/images/device.gif" width="320" alt="The display running on a SmallTV">

![Animated mascots: Clawd, the Codex robot and the Gemini sparkle](docs/images/mascots.gif)

```
┌──────────────────────────┐
│ *  19°   3:31 PM         │
│          10/6 Tue        │
├──────────────────────────┤
│ [Clawd] Claude       Max │
│ 5h [████████ 2h 5m ] 92% │
│ 7d [█████    4d 3h ] 62% │
├──────────────────────────┤
│ [robot] Codex        Pro │
│ 7d [████████ 3d 8h ] 71% │
├──────────────────────────┤
│ [star]  Antigravity      │
│ 7d [██████████  7d ]100% │
└──────────────────────────┘
```

- Top row: weather and a 12-hour clock, or **Login required** while the Mac is offline.
- One panel per service, in its own colour, with an animated mascot.
- Bars and percentages show what is **left**; stale numbers turn grey and show their age. A window
  that has reset shows 100% until the next numbers arrive.
- The reset countdown and the web page come in 19 languages: Korean, English, Japanese, Chinese
  (Simplified/Traditional), Spanish, Portuguese, French, German, Italian, Russian, Ukrainian, Polish,
  Dutch, Turkish, Vietnamese, Indonesian, Thai and Arabic. Arabic shows English units on the display.
- The weather place is searched and set on the device's web page; the clock follows its time zone.
- An admin password is optional. It is password only, with no user name, and protects settings and
  firmware updates.

> Not affiliated with GeekMagic, Anthropic, OpenAI or Google. Product names belong to their owners;
> the pixel-art mascots are fan art.

## How it works

```
Mac (always on)                       SmallTV (ESP8266)
tools/push_usage.py, launchd, 60 s    firmware/usagebar

  claude -p /usage        ─┐
  codex token → usage API  ├─ POST /api/usage ──▶  draws the screen
  agy -p /usage           ─┤
  Open-Meteo weather      ─┘ ◀── weather place ──  web page, /api/settings
```

The display never holds any login tokens. The Mac reads usage through the official CLIs, which keep
their own logins. Each service is queried every 5 minutes, and again a minute after one of its windows
resets; results are pushed every minute. If the Mac's internet goes away, which on a captive-portal
network happens when its login expires, the display says **Login required**. The check uses Apple's
captive-portal probe plus an HTTPS request, because some portals let the probe through and only
break HTTPS. While offline, the collector stops querying and the CLIs are never run,
so no sign-in browser windows pile up.

## Hardware facts and limits

ESP8266 at 80 MHz, about 40 KB free heap, 4 MB flash (Puya), ST7789 240×240 over SPI, **no buttons**,
and **USB is power only** (no serial chip). The stock panel needs its own init sequence: the generic
TFT_eSPI init leaves the screen black. See [docs/device-spec.md](docs/device-spec.md) for pins, flash
layout, measured performance and HTTPS costs.

## Safety: flashing without a serial adapter

The only way in is over the air, so a firmware that crashes before Wi-Fi comes up would brick the
device. Both firmwares therefore:

- start Wi-Fi and the HTTP `/update` page **before** the display, and fall back to an access point
  (192.168.4.1) if the Wi-Fi is unreachable or not set up yet;
- keep a crash guard in RTC memory. If the device resets during a risky task (display init, drawing,
  parsing a push), it reboots without that task;
- use the stock 4M3M flash layout, and **never mount, format or upload the LittleFS**, so the stock
  data stays intact and the stock firmware can be restored at any time;
- stay well under ~500 KB so OTA works in both directions.

Back up first: `safeboot` serves the whole 4 MB flash at `/flash.bin`.

## Repository layout

| Path | What |
|---|---|
| `firmware/usagebar/` | the usage display firmware (PlatformIO) |
| `firmware/safeboot/` | minimal diagnostics/rescue firmware: Wi-Fi, OTA, full flash dump, benchmarks at `/diag` |
| `tools/push_usage.py` | collector (standard library only), run by launchd |
| `tools/install_launchd.sh` | installs/removes the launchd agent |
| `tools/gen_glyphs.py` | bakes the countdown glyphs of every language into `src/glyphs.h` |
| `tools/gen_icons.py` | builds the animated mascots and weather icons into `src/icons.h` from `tools/icons/*.json` |
| `tools/probe.py` | runs all `safeboot` benchmarks and saves a report |
| `docs/` | device specs and images |
| `local/` | not committed: flash dumps, builds, reports, upstream clones |

## Getting started

### Which file do I flash?

| File (from [Releases](https://github.com/irsin78/geekmagictv/releases)) | |
|---|---|
| `smalltv-usagebar-*.bin` | **The display firmware. This is the one you need.** Upload it at the device's `/update` page. |
| `smalltv-safeboot-*.bin` | Optional. A rescue/diagnostics firmware, used once to back up the stock firmware before installing (step 2 below), or to recover later. |

The release files contain no passwords or tokens.

### Option A: prebuilt firmware (no build tools needed)

1. Download `smalltv-usagebar-*.bin` (and, for the backup, `smalltv-safeboot-*.bin`) from
   [Releases](https://github.com/irsin78/geekmagictv/releases).
2. **Back up the stock firmware (recommended).** On the stock firmware's page `http://<device>/update`,
   upload `smalltv-safeboot-*.bin`. After it restarts, join the open Wi-Fi **SmallTV-Safe** and download
   `http://192.168.4.1/flash.bin` (the whole 4 MB flash). Then upload `smalltv-usagebar-*.bin` at
   `http://192.168.4.1/update` using the **Firmware** field (never the FileSystem field: it would erase
   the stock data). Skipping the backup? Upload `smalltv-usagebar-*.bin` directly on the stock
   `/update` page.
3. **Wi-Fi setup.** The display shows *Wi-Fi setup*. Join the open Wi-Fi **SmallTV-Setup** with a
   phone or computer. The setup page opens by itself (or go to `http://192.168.4.1`). Press *Find
   networks*, pick your Wi-Fi, enter its password and connect. The display restarts, joins your Wi-Fi
   and shows its new address.
4. **Device page.** Open `http://<device-ip>/`, set the language, weather place and brightness, and copy
   the **push token**.
5. **On the Mac:** install and log in to the CLIs ([Claude Code](https://code.claude.com/docs/en/setup),
   [Codex](https://developers.openai.com/codex/cli),
   [Antigravity CLI](https://github.com/google-antigravity/antigravity-cli); run `claude`, `codex` and
   `agy` once each), clone this repository, and start the collector:
   ```sh
   tools/install_launchd.sh <device-ip> <push-token>
   ```
   The log is in `~/Library/Logs/smalltv-usage.log`; `tools/install_launchd.sh --uninstall` removes it.

> The Wi-Fi setup flow of the release build (step 3) has not been tested on hardware by the author:
> the test device runs a build with its Wi-Fi compiled in. Everything after it is the same code.

### Option B: build from source

1. `python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt`
2. Optional: copy `include/secrets.h.example` to `include/secrets.h` in `firmware/usagebar` and
   `firmware/safeboot` to compile in your Wi-Fi (and a `PUSH_TOKEN`, which `push_usage.py` then reads
   without `SMALLTV_TOKEN`). Without it the build behaves like the release (Wi-Fi setup page).
3. Build with `../../.venv/bin/pio run` in each firmware folder (`-e release` for files without
   secrets). Upload `.pio/build/<env>/firmware.bin` at `/update`, then continue from step 4 above.

**Restore the stock firmware:** upload the official image from
[GeekMagicClock/smalltv](https://github.com/GeekMagicClock/smalltv) (V3.1.4) at `/update`. If the
current image is large, upload a smaller one (e.g. `safeboot`) first. See the size rule in
[docs/device-spec.md](docs/device-spec.md).

## Device HTTP API (`usagebar`)

| Endpoint | |
|---|---|
| `GET /` | status and settings page |
| `GET/POST /api/usage` | current state / push new usage (`Authorization: Bearer <push token>`) |
| `GET/POST /api/settings` | brightness, language, weather place, admin password (push token shown when authorised) |
| `GET /api/wifi/scan`, `POST /api/wifi` | Wi-Fi setup |
| `POST /api/login`, `/api/logout` | admin password session (only when a password is set) |
| `GET /api/info`, `/api/log` | diagnostics |
| `POST /api/reboot` | reboot |
| `GET/POST /update` | firmware upload. Filesystem uploads are refused. |

## Notes and caveats

- Usage comes from the services' internal endpoints and CLI commands (the same ones tools like
  CodexBar use), not documented public APIs. They may change.
- The usage endpoints rate-limit (polling every minute got HTTP 429), so each service is queried every 5
  minutes (plus once right after a window resets) and backs off after errors.
- The mascot and weather pixel art was drawn by the Codex CLI on request (`tools/icons/*.json`).
  Clawd was seeded from Claude Code's welcome-banner block characters.

## Credits

- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI), [ArduinoJson](https://arduinojson.org/),
  the [ESP8266 Arduino core](https://github.com/esp8266/Arduino); `update_server.h` is adapted from
  the core's `ESP8266HTTPUpdateServer` (LGPL-2.1).
- Fonts (SIL OFL 1.1): [Galmuri](https://github.com/quiple/galmuri) and
  [GNU Unifont](https://unifoundry.com/unifont/); see `tools/fonts/`.
- Weather: [Open-Meteo](https://open-meteo.com/).
- Prior work on this hardware: [ESPHome device page](https://devices.esphome.io/devices/geekmagic-ultra/),
  [CodexBar](https://github.com/steipete/CodexBar).

## License

[MIT](LICENSE), except `firmware/usagebar/src/update_server.h`, which is adapted from the ESP8266
Arduino core and stays under LGPL-2.1. Fonts are under the SIL OFL 1.1 (see `tools/fonts/`).
