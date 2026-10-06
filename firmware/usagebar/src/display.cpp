// ST7789 240x240: usage screen.
//
// Layout: provider panels stacked at the bottom (27px name + 26px per usage row),
// a clock in whatever height is left on top. Bars and % show what is LEFT.
// Panels are drawn into an 8-bit sprite and pushed at once to avoid flicker.

#include <ESP8266WiFi.h>
#include <TFT_eSPI.h>
#include <time.h>

#include "common.h"
#include "config.h"
#include "glyphs.h"
#include "icons.h"

static const uint8_t PIN_BACKLIGHT = 5;  // active low
static const uint32_t REDRAW_MS = 20000;
static const uint32_t STALE_S = 1800;  // numbers older than this get an orange "age" badge

static TFT_eSPI tft;
static bool ready = false;
static bool redraw = true;
static uint32_t lastDraw = 0;
static uint8_t blPct = 100;

void backlightSet(uint8_t pct) {
  blPct = min<uint8_t>(pct, 100);
  analogWriteRange(1000);
  analogWrite(PIN_BACKLIGHT, 1000 - blPct * 10);
}
void displayRequestRedraw() { redraw = true; }

// TFT_eSPI's generic ST7789 init leaves this panel black (wrong VGH/VCOM/VRH and gamma).
// After tft.init() (SPI setup + hardware reset) replay the stock V3.1.4 sequence verbatim.
static void cmd(uint8_t c, std::initializer_list<uint8_t> data = {}) {
  tft.writecommand(c);
  for (uint8_t b : data) tft.writedata(b);
}

static void panelInitStock() {
  cmd(0x11); delay(120);                       // sleep out
  cmd(0xB2, {0x1F, 0x1F, 0x00, 0x33, 0x33});   // porch
  cmd(0x35, {0x00});                           // tearing effect on
  cmd(0x36, {0x00});                           // MADCTL: RGB, rotation 0
  cmd(0x3A, {0x05});                           // 16-bit colour
  cmd(0xB7, {0x00});                           // gate voltage
  cmd(0xBB, {0x36});                           // VCOM
  cmd(0xC0, {0x2C});
  cmd(0xC2, {0x01});
  cmd(0xC3, {0x13});                           // VRH
  cmd(0xC4, {0x20});                           // VDV
  cmd(0xC6, {0x13});                           // frame rate
  cmd(0xD6, {0xA1});
  cmd(0xD0, {0xA4, 0xA1});                     // power control
  cmd(0xD6, {0xA1});
  cmd(0xE0, {0xF0, 0x08, 0x0E, 0x09, 0x08, 0x04, 0x2F, 0x33, 0x45, 0x36, 0x13, 0x12, 0x2A, 0x2D});
  cmd(0xE1, {0xF0, 0x0E, 0x12, 0x0C, 0x0A, 0x15, 0x2E, 0x32, 0x44, 0x39, 0x17, 0x18, 0x2B, 0x2F});
  cmd(0xE4, {0x1D, 0x00, 0x00});
  cmd(0x21);                                   // inversion on
  cmd(0x29);                                   // display on
}

// ---------- formatting ----------

// Brand colour per service: used for the name and the bar.
static uint16_t accentFor(const char *id) {
  if (!strcmp(id, "claude")) return tft.color565(235, 125, 55);                     // orange
  if (!strcmp(id, "codex")) return tft.color565(235, 235, 235);                     // white
  if (!strcmp(id, "antigravity")) return tft.color565(66, 133, 244);  // blue
  return TFT_LIGHTGREY;
}

// Coarse age for the stale badge (one unit keeps it clear of long names): "45m ago", "3h ago", "4d ago"
static void fmtAgo(char *out, size_t n, long secs) {
  long m = max(0L, secs) / 60;
  if (m < 60) snprintf(out, n, "%ldm ago", max(1L, m));
  else if (m < 24 * 60) snprintf(out, n, "%ldh ago", m / 60);
  else snprintf(out, n, "%ldd ago", m / 1440);
}

// Time until reset in the selected language (UTF-8, glyphs from glyphs.h), e.g.
//   ko "2시간 5분" | en "2h 5m" | ja "2時間5分" | zh "2小时5分" | fr "4j 3h" | ru "2ч 5мин" | th "2ชม. 5น."
// Arabic uses English units (Unifont's Arabic is unreadable at this size).
static void fmtLeft(char *out, size_t n, long secs) {
  static const char *const UNITS[LANG_COUNT][3] = {  // day, hour, minute - indexed by Lang
      {"일", "시간", "분"},  {"d", "h", "m"},     {"日", "時間", "分"},   {"天", "小时", "分"},
      {"d", "h", "min"},    {"d", "h", "min"},   {"j", "h", "min"},     {"T", "h", "min"},
      {"g", "h", "min"},    {"天", "小時", "分"}, {"д", "ч", "мин"},     {"д", "год", "хв"},
      {"d", "godz", "min"}, {"d", "u", "min"},   {"g", "sa", "dk"},     {"ng", "h", "p"},
      {"h", "j", "m"},      {"ว.", "ชม.", "น."}, {"d", "h", "m"}};
  const char *const *u = UNITS[settings.lang < LANG_COUNT ? settings.lang : LANG_KO];
  const bool cjk = settings.lang == LANG_JA || settings.lang == LANG_ZH || settings.lang == LANG_ZH_TW;
  const char *sp = cjk ? "" : " ";
  long m = (max(0L, secs) + 59) / 60;
  long d = m / 1440, h = (m % 1440) / 60, mm = m % 60;
  if (d && h) snprintf(out, n, "%ld%s%s%ld%s", d, u[0], sp, h, u[1]);
  else if (d) snprintf(out, n, "%ld%s", d, u[0]);
  else if (h && mm) snprintf(out, n, "%ld%s%s%ld%s", h, u[1], sp, mm, u[2]);
  else if (h) snprintf(out, n, "%ld%s", h, u[1]);
  else snprintf(out, n, "%ld%s", mm, u[2]);
}

// ---------- drawing ----------

static const int NAME_H = 27;  // provider name row
static const int ROW_H = 26;   // one usage window row

static int panelHeight(const UsageProvider &p) { return NAME_H + max<int>(1, p.windows) * ROW_H; }

// ---------- baked pixel-font glyphs (digits, units of every language) ----------

static const Glyph *findGlyph(uint16_t cp) {
  for (uint8_t i = 0; i < GLYPH_COUNT; i++)
    if (GLYPHS[i].codepoint == cp) return &GLYPHS[i];
  return nullptr;
}

// Decodes the next UTF-8 code point (1-3 bytes: ASCII, Latin/Cyrillic, Hangul/CJK/Thai). 0 = end.
static uint16_t nextCp(const uint8_t *&p) {
  while (*p) {
    if (*p < 0x80) return *p++;
    if ((*p & 0xE0) == 0xC0 && p[1]) {  // 2-byte: Cyrillic, accented Latin
      uint16_t cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
      p += 2;
      return cp;
    }
    if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
      uint16_t cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      p += 3;
      return cp;
    }
    p++;  // unsupported byte: skip
  }
  return 0;
}

// Draws UTF-8 text top-left at (x, y) from the baked glyphs (glyphs.h covers every countdown string).
static void drawGlyphs(TFT_eSPI &g, const char *s, int x, int y, uint16_t color) {
  const uint8_t *p = (const uint8_t *)s;
  while (uint16_t cp = nextCp(p)) {
    if (const Glyph *gl = findGlyph(cp)) {
      g.drawBitmap(x, y, gl->bits, gl->width, GLYPH_HEIGHT, color);
      x += gl->width;
    }
  }
}

// White text with a 1px black outline: readable on the white Codex bar and the dark track alike.
static void outlinedGlyphs(TFT_eSPI &g, const char *s, int x, int y) {
  for (int dx = -1; dx <= 1; dx++)
    for (int dy = -1; dy <= 1; dy++)
      if (dx || dy) drawGlyphs(g, s, x + dx, y + dy, TFT_BLACK);
  drawGlyphs(g, s, x, y, TFT_WHITE);
}

// Static 28x28 weather icon at (x, y). Returns false when the name is unknown.
static bool drawWeatherIcon(const char *name, int x, int y) {
  for (uint8_t i = 0; i < WEATHER_ICON_COUNT; i++) {
    const WeatherIcon &w = WEATHER_ICONS[i];
    if (strcmp(w.name, name)) continue;
    for (int n = 0; n < WX_SIZE * WX_SIZE; n++) {
      const uint8_t b = pgm_read_byte(w.data + n / 2);
      const uint8_t idx = (n & 1) ? (b & 0x0F) : (b >> 4);
      if (idx) tft.drawPixel(x + n % WX_SIZE, y + n / WX_SIZE, w.palette[idx]);
    }
    return true;
  }
  return false;
}

// Weather icon with the temperature under it, in the clock row's left margin. Returns its width.
static int drawWeather(int h) {
  if (!usage.wxIcon[0] || h < 52) return 0;
  const int top = (h - (WX_SIZE + 16)) / 2;
  if (!drawWeatherIcon(usage.wxIcon, 8, top)) return 0;
  char t[12];
  snprintf(t, sizeof(t), "%d", (int)lroundf(usage.wxTemp10 / 10.0f));
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  int tw = tft.textWidth(t, 2);
  tft.drawString(t, 8 + WX_SIZE / 2 - 3, top + WX_SIZE + 1, 2);
  tft.drawCircle(8 + WX_SIZE / 2 - 3 + tw / 2 + 3, top + WX_SIZE + 4, 1, TFT_LIGHTGREY);  // degree sign
  return 8 + WX_SIZE + 4;
}

// Shown instead of the clock while the Mac reports no internet (its network login expires every 8h):
//        Login required
//    Internet offline 3h05m
static void drawNetBanner(int h) {
  if (h <= 0) return;
  tft.fillRect(0, 0, 240, h, TFT_BLACK);
  const uint16_t warn = tft.color565(245, 140, 40);
  String since = "Internet offline";
  uint32_t now = nowUnix();
  if (usage.netSince && now > usage.netSince) {
    long m = (now - usage.netSince) / 60;
    char d[16];
    if (m < 60) snprintf(d, sizeof(d), " %ldm", m);
    else if (m < 24 * 60) snprintf(d, sizeof(d), " %ldh%02ldm", m / 60, m % 60);
    else snprintf(d, sizeof(d), " %ldd%02ldh", m / 1440, (m % 1440) / 60);
    since += d;
  }
  tft.setTextDatum(MC_DATUM);
  if (h >= 52) {
    tft.setTextColor(warn, TFT_BLACK);
    tft.drawString("Login required", 120, h / 2 - 9, 4);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(since, 120, h / 2 + 15, 2);
  } else {
    tft.setTextColor(warn, TFT_BLACK);
    tft.drawString("Internet login required", 120, h / 2, 2);
  }
  tft.drawFastHLine(0, h - 1, 240, tft.color565(45, 45, 45));
}

// ---------- animated service icons (tools/gen_icons.py) ----------
// Each animation is a few sprites plus a timeline of steps ("sprite S at x,y, maybe mirrored,
// maybe another layer follows"). Frames are composed on the fly into a 40x22 cell; after each
// sequence (behaviour) the next one is picked at random. Panels are drawn into a sprite every
// REDRAW_MS; between those only the icon cells are re-pushed. Animations pause while stale.

struct IconSlot {
  const IconAnim *anim;
  int16_t x, y;      // screen position of the icon cell
  bool moving;
  uint8_t seq;       // current sequence
  uint16_t pos;      // step index of the current frame's first layer
  uint32_t lastMs;
};
static IconSlot slots[3];

static const IconAnim *animFor(const char *id) {
  for (uint8_t i = 0; i < ICON_ANIM_COUNT; i++)
    if (!strcmp(ICON_ANIMS[i].id, id)) return &ICON_ANIMS[i];
  return nullptr;
}

static IconStep stepAt(const IconAnim &a, uint16_t i) {
  IconStep st;
  memcpy_P(&st, &a.steps[i], sizeof(st));
  return st;
}

static uint16_t seqStart(const IconAnim &a, uint8_t seq) { return pgm_read_word(&a.seqStart[seq]); }

// Calls put(x, y, colour) for every opaque pixel of the frame starting at step `pos`.
template <typename Put>
static void composeFrame(const IconAnim &a, uint16_t pos, Put put) {
  for (;;) {
    const IconStep st = stepAt(a, pos);
    IconSprite sp;
    memcpy_P(&sp, &a.sprites[st.sprite], sizeof(sp));
    for (int r = 0; r < sp.h; r++) {
      for (int c = 0; c < sp.w; c++) {
        const int n = r * sp.w + c;
        const uint8_t b = pgm_read_byte(a.pixels + sp.offset + n / 2);
        const uint8_t idx = (n & 1) ? (b & 0x0F) : (b >> 4);
        if (!idx) continue;
        const int x = st.x + ((st.flags & STEP_FLIP) ? sp.w - 1 - c : c), y = st.y + r;
        if (x >= 0 && x < ICON_W && y >= 0 && y < ICON_H) put(x, y, a.palette[idx]);
      }
    }
    if (!(st.flags & STEP_LAYER)) return;
    pos++;
  }
}

// Step index of the frame after the one starting at `pos` (skipping its extra layers).
static uint16_t nextFrame(const IconAnim &a, uint16_t pos) {
  while (stepAt(a, pos).flags & STEP_LAYER) pos++;
  return pos + 1;
}

static void startSeq(IconSlot &s, uint8_t seq) {
  s.seq = seq;
  s.pos = seqStart(*s.anim, seq);
}

// Draws the current frame onto `g` (the panel sprite) with the cell's top-left at (x, y).
static void drawIconFrame(TFT_eSPI &g, const IconSlot &s, int x, int y) {
  composeFrame(*s.anim, s.pos, [&](int px, int py, uint16_t c) { g.drawPixel(x + px, y + py, c); });
}

// Pushes the current frame straight to the panel (black where transparent), flicker-free.
static void pushIconFrame(const IconSlot &s) {
  static uint16_t buf[ICON_W * ICON_H];
  for (uint16_t &px : buf) px = TFT_BLACK;
  composeFrame(*s.anim, s.pos, [&](int px, int py, uint16_t c) { buf[py * ICON_W + px] = c; });
  tft.setSwapBytes(true);
  tft.pushImage(s.x, s.y, ICON_W, ICON_H, buf);
  tft.setSwapBytes(false);
}

static void animateIcons() {
  const uint32_t now = millis();
  for (IconSlot &s : slots) {
    if (!s.anim || !s.moving) continue;
    if (now - s.lastMs < 1000u / max<uint8_t>(1, s.anim->fps)) continue;
    s.lastMs = now;
    s.pos = nextFrame(*s.anim, s.pos);
    if (s.pos >= seqStart(*s.anim, s.seq + 1)) {
      // Sequence done: pick another behaviour (avoid repeating the same one when possible).
      uint8_t n = s.anim->seqCount, next = random(n);
      if (n > 1 && next == s.seq) next = (next + 1 + random(n - 1)) % n;
      startSeq(s, next);
    }
    guardBegin("disp_anim");  // a crash here must disable the display on the next boot, not loop
    pushIconFrame(s);
    guardEnd();
  }
}

// Clock row on top, sized to whatever height the panels leave free.
// 12-hour clock with AM/PM and a small date, e.g.  "6:41  PM"
//                                                 "      9/30 Wed"
static void drawClock(int h) {
  if (h <= 0) return;
  tft.fillRect(0, 0, 240, h, TFT_BLACK);
  static const char *const WDAY[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  char hm[16] = "--:--", ampm[4] = "", date[24] = "";
  time_t t = nowUnix();  // NTP, or estimated from the last push until NTP syncs
  if (t > 1700000000) {
    struct tm lt;
    localtime_r(&t, &lt);
    int h12 = lt.tm_hour % 12 ? lt.tm_hour % 12 : 12;
    snprintf(hm, sizeof(hm), "%d:%02d", h12, lt.tm_min);
    strlcpy(ampm, lt.tm_hour < 12 ? "AM" : "PM", sizeof(ampm));
    snprintf(date, sizeof(date), "%d/%d %s", lt.tm_mon + 1, lt.tm_mday, WDAY[lt.tm_wday]);
  }

  const int left = drawWeather(h);  // weather on the left, the clock centred in the rest
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (h >= 52) {
    // Big time on the left, AM/PM over the date on the right; centre the whole group.
    const int gap = 10;
    int timeW = tft.textWidth(hm, 6);
    int sideW = max(tft.textWidth(ampm, 4), tft.textWidth(date, 2));
    int x = left + (240 - left - (timeW + gap + sideW)) / 2;
    int top = (h - 48) / 2;
    tft.setTextDatum(TL_DATUM);
    tft.drawString(hm, x, top, 6);
    tft.drawString(ampm, x + timeW + gap, top, 4);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(date, x + timeW + gap, top + 30, 2);
  } else if (h >= 28) {
    tft.setTextDatum(MC_DATUM);
    tft.drawString(String(hm) + " " + ampm + "  " + date, 120, h / 2, 4);
  }
  tft.drawFastHLine(0, h - 1, 240, tft.color565(45, 45, 45));
}

// One provider panel drawn onto `g` with its top at y0 (0 for a sprite).
//   [Name (26px)                      plan]
//   [5h  [=====bar 2h05m=====]      95% ]    bar and % show what is LEFT
static void drawPanel(TFT_eSPI &g, int y0, const UsageProvider &p, uint32_t now, uint32_t pushAgeS, bool last,
                      IconSlot &slot) {
  // Age of the numbers themselves (the Mac keeps resending cached values while a service fails).
  const uint32_t ageS = (p.fetchedAt && now > p.fetchedAt) ? now - p.fetchedAt : pushAgeS;
  const int h = panelHeight(p);
  const uint16_t accent = accentFor(p.id);
  g.fillRect(0, y0, 240, h, TFT_BLACK);
  const IconAnim *anim = animFor(p.id);
  if (anim != slot.anim || (anim && slot.pos >= seqStart(*anim, anim->seqCount))) {
    slot.anim = anim;
    if (anim) startSeq(slot, random(anim->seqCount));
  }
  if (slot.anim) drawIconFrame(g, slot, 4, y0 + (NAME_H - ICON_H) / 2);
  g.setTextDatum(TL_DATUM);
  g.setTextColor(accent, TFT_BLACK);
  g.drawString(p.name, slot.anim ? 4 + ICON_W + 5 : 4, y0 + 1, 4);

  // Top-right status: "Log in" (this service's CLI is logged out) > "3h ago" (old numbers) > plan.
  const bool needsLogin = !strcmp(p.error, "login");
  g.setTextDatum(TR_DATUM);
  if (needsLogin || (ageS > STALE_S && p.windows)) {
    char s[16];
    if (needsLogin) strlcpy(s, "Log in", sizeof(s));
    else fmtAgo(s, sizeof(s), ageS);
    g.setTextColor(tft.color565(245, 140, 40), TFT_BLACK);
    g.drawString(s, 236, y0 + 7, 2);
  } else if (p.plan[0]) {
    g.setTextColor(TFT_DARKGREY, TFT_BLACK);
    g.drawString(p.plan, 236, y0 + 7, 2);
  }

  if (p.error[0] && !p.windows && !needsLogin) {
    g.setTextDatum(TL_DATUM);
    g.setTextColor(tft.color565(235, 64, 52), TFT_BLACK);
    g.drawString(p.error, 4, y0 + NAME_H + 5, 2);
  }

  // Numbers are only "last known" while the Mac is offline or they are old: draw them dimmed.
  const bool stale = !strcmp(usage.net, "login") || ageS > STALE_S;
  const uint16_t dim = tft.color565(95, 95, 95);
  slot.moving = !stale && !p.error[0];  // icons rest while the numbers are not current

  for (uint8_t w = 0; w < p.windows; w++) {
    const UsageWindow &x = p.win[w];
    const int ry = y0 + NAME_H + w * ROW_H;
    // A window that reset after the numbers were fetched holds no valid value any more.
    const bool resetSinceFetch = x.resetAt && now >= x.resetAt && p.fetchedAt && p.fetchedAt < x.resetAt;
    const int left = (x.used < 0 || resetSinceFetch) ? -1 : 100 - x.used;

    g.setTextDatum(TL_DATUM);
    g.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    g.drawString(x.label, 4, ry + 4, 2);

    const int bx = 36, bw = 136, bh = 22, by = ry;
    g.fillRoundRect(bx, by, bw, bh, 4, tft.color565(50, 50, 50));
    if (left > 0) g.fillRoundRect(bx, by, max(8, left * bw / 100), bh, 4, stale ? dim : accent);

    if (x.resetAt && now && !resetSinceFetch) {
      char t[32];
      fmtLeft(t, sizeof(t), (long)x.resetAt - (long)now);
      outlinedGlyphs(g, t, bx + 6, by + 3);
    }

    char pct[8];
    if (left < 0) strlcpy(pct, "--", sizeof(pct));
    else snprintf(pct, sizeof(pct), "%d%%", left);
    g.setTextDatum(TR_DATUM);
    g.setTextColor(stale ? dim : TFT_WHITE, TFT_BLACK);
    g.drawString(pct, 238, ry - 1, 4);
  }
  if (!last) g.drawFastHLine(0, y0 + h - 1, 240, tft.color565(45, 45, 45));
}

static void drawWaiting() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  if (!wifiConfigured()) {
    // First boot of a release build: guide the user through the setup access point.
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Wi-Fi setup", 120, 60, 4);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("1. Join this Wi-Fi:", 120, 100, 2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(AP_SSID, 120, 126, 4);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("2. Open in a browser:", 120, 160, 2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(WiFi.softAPIP().toString(), 120, 186, 4);
    return;
  }
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Waiting for data", 120, 96, 4);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString((apMode ? WiFi.softAPIP() : WiFi.localIP()).toString(), 120, 130, 4);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("POST /api/usage", 120, 162, 2);
}

static void drawAll() {
  guardBegin("disp_draw");
  // Slots of panels that are not drawn any more must stop animating.
  for (uint8_t i = usage.have ? usage.count : 0; i < 3; i++) slots[i].anim = nullptr;
  if (!usage.have) {
    drawWaiting();
  } else {
    int panelsH = 0;
    for (uint8_t i = 0; i < usage.count; i++) panelsH += panelHeight(usage.p[i]);
    int y = max(0, 240 - panelsH);  // panels sit at the bottom, the clock takes the rest
    if (!strcmp(usage.net, "login")) drawNetBanner(y);
    else drawClock(y);

    uint32_t now = nowUnix();
    uint32_t pushAgeS = (millis() - usage.receivedMs) / 1000;
    TFT_eSprite spr(&tft);
    spr.setColorDepth(8);
    bool useSprite = spr.createSprite(240, NAME_H + 2 * ROW_H) != nullptr;
    for (uint8_t i = 0; i < usage.count && y < 240; i++) {
      bool last = i == usage.count - 1;
      IconSlot &slot = slots[i];
      slot.x = 4;
      slot.y = y + (NAME_H - ICON_H) / 2;
      if (useSprite) {
        spr.fillSprite(TFT_BLACK);  // panels differ in height: clear rows left from the previous one
        drawPanel(spr, 0, usage.p[i], now, pushAgeS, last, slot);
        spr.pushSprite(0, y);  // rows below this panel are black and get overdrawn by the next one
      } else {
        drawPanel(tft, y, usage.p[i], now, pushAgeS, last, slot);
      }
      if (slot.anim) pushIconFrame(slot);  // 16-bit colours, same as between redraws
      y += panelHeight(usage.p[i]);
      yield();
    }
    if (useSprite) spr.deleteSprite();
  }
  guardEnd();
  lastDraw = millis();
  redraw = false;
}

void displayBoot() {
  guardBegin("disp_init");
  tft.init();
  panelInitStock();
  tft.fillScreen(TFT_BLACK);
  ready = true;
  guardEnd();
  logf("display ready");
  drawAll();
}

void displayLoop() {
  if (!ready) return;
  if (redraw || millis() - lastDraw >= REDRAW_MS) drawAll();
  else if (usage.have) animateIcons();
}
