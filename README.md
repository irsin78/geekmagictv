# SmallTV AI usage display

Custom firmware that turns a **GeekMagic SmallTV** (ESP8266, 240×240 display) into a desk display
for AI coding-assistant quotas: **Claude Code, Codex and Antigravity**. It shows how much of each
limit is left, a countdown to the next reset, a clock and the weather, with little animated mascots.

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
- Bars and percentages show what is **left**; stale numbers turn grey and show their age.
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
their own logins. Each service is queried at most every 5 minutes, and results are pushed every minute.
If the Mac's internet goes away, which on a captive-portal network happens when its login expires, the
display says **Login required**. In that case the collector stops querying and the CLIs are never run,
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
  (`SmallTV-Safe`, 192.168.4.1) if the Wi-Fi is unreachable;
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

1. **Tools**
   ```sh
   python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
   ```
2. **Secrets.** Copy `include/secrets.h.example` to `include/secrets.h` in `firmware/safeboot` and
   `firmware/usagebar`, then fill in your Wi-Fi details and a random `PUSH_TOKEN`.
3. **Back up the stock firmware.** Build `safeboot` (`cd firmware/safeboot && ../../.venv/bin/pio run`).
   Upload `.pio/build/smalltv/firmware.bin` on the stock firmware's `http://<device>/update` page, then
   download `http://<device>/flash.bin`.
4. **Flash the display firmware.** Build `firmware/usagebar` the same way and upload it at `/update`.
5. **Log in to the CLIs on the Mac.** Install [Claude Code](https://code.claude.com/docs/en/setup),
   [Codex](https://developers.openai.com/codex/cli) and the
   [Antigravity CLI](https://github.com/google-antigravity/antigravity-cli). Run `claude`, `codex` and
   `agy` once each and sign in.
6. **Start the collector.**
   ```sh
   tools/install_launchd.sh <device-ip>
   ```
   The log is in `~/Library/Logs/smalltv-usage.log`. Remove the agent with `tools/install_launchd.sh --uninstall`.
7. **Settings.** Open `http://<device-ip>/` to set the language, weather place, brightness and an
   optional admin password.

**Restore the stock firmware:** upload the official image from
[GeekMagicClock/smalltv](https://github.com/GeekMagicClock/smalltv) (V3.1.4) at `/update`. If the
current image is large, upload a smaller one (e.g. `safeboot`) first. See the size rule in
[docs/device-spec.md](docs/device-spec.md).

## Device HTTP API (`usagebar`)

| Endpoint | |
|---|---|
| `GET /` | status and settings page |
| `GET/POST /api/usage` | current state / push new usage (`Authorization: Bearer PUSH_TOKEN`) |
| `GET/POST /api/settings` | brightness, language, weather place, admin password |
| `POST /api/login`, `/api/logout` | admin password session (only when a password is set) |
| `GET /api/info`, `/api/log` | diagnostics |
| `POST /api/reboot` | reboot |
| `GET/POST /update` | firmware upload. Filesystem uploads are refused. |

## Notes and caveats

- Usage comes from the services' internal endpoints and CLI commands (the same ones tools like
  CodexBar use), not documented public APIs. They may change.
- Polling `api.anthropic.com/api/oauth/usage` every minute triggered HTTP 429. Five minutes is fine.
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
