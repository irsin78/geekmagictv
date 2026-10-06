// ST7789 240x240 panel: status screen, visual test patterns, throughput benchmarks.

#include <ESP8266WiFi.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>

#include "diag.h"
#include "test_jpg.h"

static const uint8_t PIN_BACKLIGHT = 5;  // active low
static const uint32_t SPI_DEFAULT_HZ = 40000000;

static TFT_eSPI tft;
static bool ready = false;
static bool jpgDraw = true;  // false = decode only (benchmark)
static uint32_t spiHz = SPI_DEFAULT_HZ;

void backlightSet(uint8_t pct) {
  pct = min<uint8_t>(pct, 100);
  analogWriteRange(1000);
  analogWrite(PIN_BACKLIGHT, 1000 - pct * 10);
}

static bool jpgOut(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bmp) {
  if (y >= tft.height()) return false;
  if (jpgDraw) tft.pushImage(x, y, w, h, bmp);
  return true;
}

// ---------- screens ----------

static void drawStatus() {
  String ip = (apMode ? WiFi.softAPIP() : WiFi.localIP()).toString();
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("SmallTV diag " SAFEBOOT_VERSION, 4, 4, 4);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(ip, 4, 34, 4);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char line[48];
  int y = 64;
  auto row = [&](const char *s) { tft.drawString(s, 4, y, 2); y += 17; };
  snprintf(line, sizeof(line), "heap %u  blk %u", ESP.getFreeHeap(), ESP.getMaxFreeBlockSize()); row(line);
  snprintf(line, sizeof(line), "flash %uKB id %06x", ESP.getFlashChipRealSize() / 1024, ESP.getFlashChipId()); row(line);
  snprintf(line, sizeof(line), "sketch %uKB  freeOTA %uKB", ESP.getSketchSize() / 1024, ESP.getFreeSketchSpace() / 1024); row(line);
  snprintf(line, sizeof(line), "rssi %d dBm  ch %d", WiFi.RSSI(), WiFi.channel()); row(line);
  snprintf(line, sizeof(line), "reset: %s", ESP.getResetReason().c_str()); row(line);
  if (lastCrashedTask()[0]) {
    snprintf(line, sizeof(line), "crashed in: %s", lastCrashedTask()); row(line);
  }
  snprintf(line, sizeof(line), "http://%s/diag", ip.c_str()); row(line);

  // Colour check: each bar is labelled with the colour it should be.
  const uint16_t cols[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE};
  const char *names[] = {"R", "G", "B", "W"};
  for (int i = 0; i < 4; i++) {
    tft.fillRect(i * 60, 200, 60, 40, cols[i]);
    tft.setTextColor(TFT_BLACK, cols[i]);
    tft.drawString(names[i], i * 60 + 24, 208, 4);
  }
}

static void drawBars() {
  const uint16_t cols[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_CYAN, TFT_MAGENTA, TFT_YELLOW, TFT_WHITE, TFT_BLACK};
  const char *names[] = {"R", "G", "B", "C", "M", "Y", "W", "K"};
  for (int i = 0; i < 8; i++) {
    tft.fillRect(i * 30, 0, 30, 240, cols[i]);
    tft.setTextColor(i == 7 ? TFT_WHITE : TFT_BLACK, cols[i]);
    tft.drawString(names[i], i * 30 + 9, 110, 2);
  }
}

static void drawGrid() {
  tft.fillScreen(TFT_BLACK);
  for (int i = 0; i < 240; i += 20) {
    tft.drawFastVLine(i, 0, 240, TFT_DARKGREY);
    tft.drawFastHLine(0, i, 240, TFT_DARKGREY);
  }
  tft.drawRect(0, 0, 240, 240, TFT_RED);  // must be fully visible on all 4 edges
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("TL", 4, 4, 2);
  tft.drawString("TR", 214, 4, 2);
  tft.drawString("BL", 4, 220, 2);
  tft.drawString("BR", 214, 220, 2);
  tft.drawString("UP ^", 100, 100, 4);
}

static void drawGradient() {
  // 4 ramps: grey, red, green, blue. Smooth = 16-bit colour works; visible steps are normal (5/6 bit).
  for (int x = 0; x < 240; x++) {
    uint8_t v = x * 255 / 239;
    tft.drawFastVLine(x, 0, 60, tft.color565(v, v, v));
    tft.drawFastVLine(x, 60, 60, tft.color565(v, 0, 0));
    tft.drawFastVLine(x, 120, 60, tft.color565(0, v, 0));
    tft.drawFastVLine(x, 180, 60, tft.color565(0, 0, v));
  }
}

static void drawText() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Font1 GLCD 6x8 The quick brown fox", 2, 2, 1);
  tft.drawString("Font2 16px quick brown fox", 2, 14, 2);
  tft.drawString("Font4 26px Hello", 2, 34, 4);
  tft.setTextSize(2);
  tft.drawString("GLCD x2 ABC 123", 2, 70, 1);
  tft.setTextSize(3);
  tft.drawString("x3 12:34", 2, 92, 1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Korean needs a custom font", 2, 130, 2);
}

static void drawJpeg() {
  uint8_t *buf = (uint8_t *)malloc(TEST_JPG_LEN);
  if (!buf) return;
  memcpy_P(buf, TEST_JPG, TEST_JPG_LEN);
  jpgDraw = true;
  TJpgDec.drawJpg(0, 0, buf, TEST_JPG_LEN);
  free(buf);
}

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

void displayBoot() {
  guardBegin("disp_init");
  tft.init();
  panelInitStock();
  tft.setSwapBytes(true);
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(jpgOut);
  backlightSet(100);
  ready = true;
  drawStatus();
  guardEnd();
  logf("display ready");
}

// ---------- HTTP handlers ----------

static bool needReady() {
  if (ready) return true;
  server.send(409, F("text/plain"), F("display not initialised (GET /api/display/init)"));
  return false;
}

static void handleInit() {
  displayBoot();
  Json j;
  j.flag("ready", ready).send();
}

static void handlePattern() {
  if (!needReady()) return;
  String n = server.arg("n");
  guardBegin("disp_pattern");
  if (n == "bars") drawBars();
  else if (n == "grid") drawGrid();
  else if (n == "gradient") drawGradient();
  else if (n == "text") drawText();
  else if (n == "jpeg") drawJpeg();
  else drawStatus();
  guardEnd();
  Json j;
  j.str("pattern", n.length() ? n : String("status")).send();
}

// GET /api/display/cfg?invert=0|1&rot=0..3&madctl=0xNN&spi=MHz&bl=0..100
static void handleCfg() {
  if (!needReady()) return;
  Json j;
  if (server.hasArg("invert")) { tft.invertDisplay(server.arg("invert") == "1"); j.str("invert", server.arg("invert")); }
  if (server.hasArg("rot")) { tft.setRotation(server.arg("rot").toInt() & 3); j.num("rot", tft.getRotation()); }
  if (server.hasArg("madctl")) {
    uint8_t v = strtoul(server.arg("madctl").c_str(), nullptr, 0);
    tft.writecommand(0x36);
    tft.writedata(v);
    j.num("madctl", v);
  }
  if (server.hasArg("spi")) {
    spiHz = server.arg("spi").toInt() * 1000000UL;
    SPI.setFrequency(spiHz);
    j.num("spi_hz", spiHz);
  }
  if (server.hasArg("bl")) { backlightSet(server.arg("bl").toInt()); j.num("bl", server.arg("bl").toInt()); }
  j.send();
}

static uint32_t timeFill(uint32_t hz, int reps) {
  SPI.setFrequency(hz);
  uint32_t t = micros();
  for (int i = 0; i < reps; i++) tft.fillScreen(i & 1 ? TFT_NAVY : TFT_MAROON);
  return (micros() - t) / reps;
}

static void handleBench() {
  if (!needReady()) return;
  guardBegin("disp_bench");
  Json j;
  j.num("fill_us_40mhz", timeFill(40000000, 6)).num("fill_us_80mhz", timeFill(80000000, 6));
  SPI.setFrequency(spiHz);
  yield();

  // Full frame streamed from a 240x20 RAM strip (what a "push pixels" pipeline costs).
  uint16_t *strip = (uint16_t *)malloc(240 * 20 * 2);
  if (strip) {
    for (int i = 0; i < 240 * 20; i++) strip[i] = tft.color565(i % 240, (i / 240) * 12, 128);
    uint32_t t = micros();
    for (int y = 0; y < 240; y += 20) tft.pushImage(0, y, 240, 20, strip);
    j.num("push_frame_us", micros() - t);
    free(strip);
  }
  yield();

  uint32_t t = micros();
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  for (int i = 0; i < 50; i++) tft.drawString("12:34:56 Temp 23.5C", 0, (i % 14) * 17, 2);
  j.num("text_50_lines_us", micros() - t);
  yield();

  // JPEG: decode-only vs decode+draw, embedded 240x240 test image.
  uint8_t *buf = (uint8_t *)malloc(TEST_JPG_LEN);
  if (buf) {
    memcpy_P(buf, TEST_JPG, TEST_JPG_LEN);
    jpgDraw = false;
    t = micros();
    TJpgDec.drawJpg(0, 0, buf, TEST_JPG_LEN);
    j.num("jpeg_decode_us", micros() - t);
    jpgDraw = true;
    t = micros();
    TJpgDec.drawJpg(0, 0, buf, TEST_JPG_LEN);
    j.num("jpeg_decode_draw_us", micros() - t).num("jpeg_bytes", TEST_JPG_LEN);
    free(buf);
  }
  yield();

  // Sprites (off-screen buffers) that fit in RAM.
  struct { const char *name; uint8_t depth; int w, h; } sp[] = {
      {"sprite16_240x60", 16, 240, 60}, {"sprite8_240x240", 8, 240, 240}, {"sprite1_240x240", 1, 240, 240},
  };
  for (auto &s : sp) {
    TFT_eSprite spr(&tft);
    spr.setColorDepth(s.depth);
    bool ok = spr.createSprite(s.w, s.h) != nullptr;
    Json r;
    r.flag("ok", ok);
    if (ok) {
      spr.fillSprite(TFT_DARKCYAN);
      spr.setTextColor(TFT_WHITE);
      spr.drawString(s.name, 4, 4, 2);
      t = micros();
      spr.pushSprite(0, 0);
      r.num("push_us", micros() - t);
      spr.deleteSprite();
    }
    j.raw(s.name, r.done());
    yield();
  }
  j.num("heap_after", ESP.getFreeHeap());
  guardEnd();
  drawStatus();
  j.send();
}

// POST /api/display/raw (multipart): 240*240 RGB565 big-endian -> straight to the panel.
static uint32_t rawBytes = 0, rawStart = 0, rawUs = 0;
static int rawCarry = -1;
static uint16_t *rawPix = nullptr;  // 1KB, allocated per upload
static const size_t RAW_PIX_BYTES = 1024;

static void handleRawDone() {
  Json j;
  j.num("bytes", rawBytes).num("us", rawUs).send();
}

static void handleRawUpload() {
  HTTPUpload &u = server.upload();
  if (!ready) return;
  if (u.status == UPLOAD_FILE_START) {
    guardBegin("disp_raw");
    rawBytes = 0;
    rawCarry = -1;
    rawStart = micros();
    rawPix = (uint16_t *)malloc(RAW_PIX_BYTES);
    tft.setSwapBytes(false);  // bytes already in panel order
    tft.startWrite();
    tft.setAddrWindow(0, 0, 240, 240);
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (!rawPix) return;
    const uint8_t *p = u.buf;
    size_t n = u.currentSize;
    rawBytes += n;
    uint8_t *out = reinterpret_cast<uint8_t *>(rawPix);
    size_t o = 0;
    if (rawCarry >= 0 && n) { out[o++] = rawCarry; out[o++] = *p++; n--; rawCarry = -1; }
    while (n >= 2) {
      size_t take = min(n & ~(size_t)1, RAW_PIX_BYTES - o);
      memcpy(out + o, p, take);  // memcpy: source may be unaligned
      o += take; p += take; n -= take;
      if (o == RAW_PIX_BYTES) { tft.pushPixels(rawPix, o / 2); o = 0; }
    }
    if (o) tft.pushPixels(rawPix, o / 2);
    if (n) rawCarry = *p;
  } else if (u.status == UPLOAD_FILE_END || u.status == UPLOAD_FILE_ABORTED) {
    tft.endWrite();
    tft.setSwapBytes(true);
    free(rawPix);
    rawPix = nullptr;
    rawUs = micros() - rawStart;
    guardEnd();
  }
}

// POST /api/display/jpeg (multipart): buffer in RAM, decode, draw.
static uint8_t *jpgBuf = nullptr;
static uint32_t jpgCap = 0, jpgLen = 0, jpgRecvStart = 0, jpgRecvUs = 0, jpgDecodeUs = 0;
static bool jpgOverflow = false;

static void handleJpegDone() {
  Json j;
  j.num("bytes", jpgLen).flag("overflow", jpgOverflow).num("cap", jpgCap)
      .num("recv_us", jpgRecvUs).num("decode_draw_us", jpgDecodeUs).send();
}

static void handleJpegUpload() {
  HTTPUpload &u = server.upload();
  if (!ready) return;
  if (u.status == UPLOAD_FILE_START) {
    free(jpgBuf);
    jpgCap = min<uint32_t>(ESP.getMaxFreeBlockSize() > 12000 ? ESP.getMaxFreeBlockSize() - 12000 : 0, 48000);
    jpgBuf = (uint8_t *)malloc(jpgCap);
    jpgLen = 0;
    jpgOverflow = jpgBuf == nullptr;
    jpgRecvStart = micros();
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (!jpgOverflow && jpgLen + u.currentSize <= jpgCap) {
      memcpy(jpgBuf + jpgLen, u.buf, u.currentSize);
      jpgLen += u.currentSize;
    } else {
      jpgOverflow = true;
    }
  } else if (u.status == UPLOAD_FILE_END) {
    jpgRecvUs = micros() - jpgRecvStart;
    jpgDecodeUs = 0;
    if (!jpgOverflow && jpgLen) {
      guardBegin("disp_jpeg");
      jpgDraw = true;
      uint32_t t = micros();
      TJpgDec.drawJpg(0, 0, jpgBuf, jpgLen);
      jpgDecodeUs = micros() - t;
      guardEnd();
    }
    free(jpgBuf);
    jpgBuf = nullptr;
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    free(jpgBuf);
    jpgBuf = nullptr;
  }
}

// GET /api/display/stockinit?color=0xF800  re-run the panel init and fill one colour
static void handleStockInit() {
  uint16_t color = server.hasArg("color") ? strtoul(server.arg("color").c_str(), nullptr, 0) : 0xF800;
  displayBoot();
  uint32_t t = micros();
  tft.fillScreen(color);
  Json j;
  j.str("color", "0x" + String(color, HEX)).num("fill_us", micros() - t).send();
}

// GET /api/display/blink?n=10  toggle the backlight pin every 500ms (ends ON = LOW)
static void handleBlink() {
  int n = constrain(server.arg("n").toInt(), 2, 20);
  pinMode(PIN_BACKLIGHT, OUTPUT);
  for (int i = 0; i < n; i++) {
    digitalWrite(PIN_BACKLIGHT, i & 1 ? LOW : HIGH);
    delay(500);
  }
  digitalWrite(PIN_BACKLIGHT, LOW);
  Json j;
  j.num("toggles", n).send();
}

void displayRegister() {
  server.on("/api/display/stockinit", HTTP_GET, handleStockInit);
  server.on("/api/display/blink", HTTP_GET, handleBlink);
  server.on("/api/display/init", HTTP_GET, handleInit);
  server.on("/api/display/pattern", HTTP_GET, handlePattern);
  server.on("/api/display/cfg", HTTP_GET, handleCfg);
  server.on("/api/display/bench", HTTP_GET, handleBench);
  server.on("/api/display/raw", HTTP_POST, handleRawDone, handleRawUpload);
  server.on("/api/display/jpeg", HTTP_POST, handleJpegDone, handleJpegUpload);
}
