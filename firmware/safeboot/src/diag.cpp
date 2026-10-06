// System info, benchmarks, input probing, event log, crash guard.

#include <ESP8266WiFi.h>
#ifdef DIAG_TLS
#include <WiFiClientSecure.h>
#endif
#include <stdarg.h>

#include "diag.h"

extern "C" uint32_t _FS_start;
extern "C" uint32_t _FS_end;

// ---------- crash guard (RTC user memory survives resets, not power loss) ----------

struct Guard {
  uint32_t magic;
  char task[28];
};
static const uint32_t GUARD_MAGIC = 0x5AFEB007;
static char crashedTask[28] = "";

static void guardWrite(const char *task) {
  Guard g{GUARD_MAGIC, {0}};
  strlcpy(g.task, task, sizeof(g.task));
  ESP.rtcUserMemoryWrite(0, reinterpret_cast<uint32_t *>(&g), sizeof(g));
}

void guardInit() {
  Guard g;
  ESP.rtcUserMemoryRead(0, reinterpret_cast<uint32_t *>(&g), sizeof(g));
  if (g.magic == GUARD_MAGIC && g.task[0]) {
    g.task[sizeof(g.task) - 1] = 0;
    strlcpy(crashedTask, g.task, sizeof(crashedTask));
  }
  guardWrite("");
}
void guardBegin(const char *task) { guardWrite(task); }
void guardEnd() { guardWrite(""); }
const char *lastCrashedTask() { return crashedTask; }

// ---------- log ----------

static const int LOG_LINES = 24;
static char logBuf[LOG_LINES][80];
static int logHead = 0, logCount = 0;

void logf(const char *fmt, ...) {
  char *line = logBuf[logHead];
  int n = snprintf(line, sizeof(logBuf[0]), "[%7lu] ", millis());
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line + n, sizeof(logBuf[0]) - n, fmt, ap);
  va_end(ap);
  Serial.println(line);
  logHead = (logHead + 1) % LOG_LINES;
  if (logCount < LOG_LINES) logCount++;
}

static void handleLog() {
  String out;
  out.reserve(logCount * 64);
  for (int i = 0; i < logCount; i++) {
    out += logBuf[(logHead - logCount + i + LOG_LINES) % LOG_LINES];
    out += '\n';
  }
  server.send(200, F("text/plain"), out);
}

// ---------- system info ----------

static const char *phyName(WiFiPhyMode_t m) {
  switch (m) {
    case WIFI_PHY_MODE_11B: return "11b";
    case WIFI_PHY_MODE_11G: return "11g";
    case WIFI_PHY_MODE_11N: return "11n";
    default: return "?";
  }
}

String sysInfoJson() {
  rst_info *ri = ESP.getResetInfoPtr();
  Json j;
  j.str("safeboot", SAFEBOOT_VERSION)
      .str("core", ESP.getCoreVersion())
      .str("sdk", ESP.getSdkVersion())
      .num("cpu_mhz", ESP.getCpuFreqMHz())
      .str("chip_id", String(ESP.getChipId(), HEX))
      .str("flash_id", String(ESP.getFlashChipId(), HEX))
      .num("flash_real", ESP.getFlashChipRealSize())
      .num("flash_cfg", ESP.getFlashChipSize())
      .num("flash_hz", ESP.getFlashChipSpeed())
      .num("flash_mode", (int)ESP.getFlashChipMode())
      .num("sketch", ESP.getSketchSize())
      .num("free_ota", ESP.getFreeSketchSpace())
      .str("fs_start", "0x" + String((uint32_t)&_FS_start - 0x40200000, HEX))
      .str("fs_end", "0x" + String((uint32_t)&_FS_end - 0x40200000, HEX))
      .num("heap_free", ESP.getFreeHeap())
      .num("heap_max_block", ESP.getMaxFreeBlockSize())
      .num("heap_frag_pct", ESP.getHeapFragmentation())
      .num("stack_free", ESP.getFreeContStack())
      .str("reset", ESP.getResetReason())
      .num("reset_cause", ri->reason)
      .num("exc_cause", ri->exccause)
      .str("exc_epc1", "0x" + String(ri->epc1, HEX))
      .str("crashed_task", lastCrashedTask())
      .flag("ap_mode", apMode)
      .str("ip", (apMode ? WiFi.softAPIP() : WiFi.localIP()).toString())
      .str("mac", WiFi.macAddress())
      .str("ssid", WiFi.SSID())
      .str("bssid", WiFi.BSSIDstr())
      .num("channel", WiFi.channel())
      .num("rssi", WiFi.RSSI())
      .str("phy", phyName(WiFi.getPhyMode()))
      .num("uptime_s", millis() / 1000);
  return j.done();
}

static void handleInfo() { server.send(200, F("application/json"), sysInfoJson()); }

// ---------- CPU / memory / flash benchmarks ----------

static uint32_t benchInt() {
  volatile uint32_t x = 2463534242UL;
  uint32_t t = micros();
  for (uint32_t i = 0; i < 200000; i++) {
    uint32_t v = x;
    v ^= v << 13; v ^= v >> 17; v ^= v << 5;
    x = v;
  }
  return micros() - t;
}

static uint32_t benchFloat() {
  volatile float f = 1.0001f;
  float a = 0.5f;
  uint32_t t = micros();
  for (int i = 0; i < 20000; i++) a = a * f + 0.25f;
  f = a;
  return micros() - t;
}

static uint32_t benchMemcpyKBs() {
  const size_t n = 8192;
  uint8_t *a = (uint8_t *)malloc(n), *b = (uint8_t *)malloc(n);
  if (!a || !b) { free(a); free(b); return 0; }
  memset(a, 0x5A, n);
  uint32_t t = micros();
  for (int i = 0; i < 32; i++) {
    a[i] = i;
    memcpy(b, a, n);
    asm volatile("" : : "r"(b) : "memory");  // keep the copy: result is observed below
  }
  uint32_t us = micros() - t;
  volatile uint8_t sink = b[n - 1];
  (void)sink;
  free(a); free(b);
  return us ? (uint64_t)n * 32 * 1000000 / 1024 / us : 0;
}

// GET /api/bench/cpu[?part=int|float|memcpy] at the current clock.
// Each part has its own guard name so a crash pinpoints the culprit.
static void handleBenchCpu() {
  String part = server.arg("part");
  Json j;
  j.num("cpu_mhz", ESP.getCpuFreqMHz());
  if (!part.length() || part == "int") {
    guardBegin("cpu_int");
    j.num("int_200k_xorshift_us", benchInt());
    yield();
  }
  if (!part.length() || part == "float") {
    guardBegin("cpu_float");
    j.num("float_20k_fma_us", benchFloat());
    yield();
  }
  if (!part.length() || part == "memcpy") {
    guardBegin("cpu_memcpy");
    j.num("memcpy_kbs", benchMemcpyKBs());
  }
  guardEnd();
  j.send();
}

// GET /api/cpu?mhz=80|160  switch the CPU clock (not persisted across reboot).
static void handleCpuFreq() {
  uint8_t mhz = server.arg("mhz").toInt() == 160 ? SYS_CPU_160MHZ : SYS_CPU_80MHZ;
  guardBegin("cpu_freq");
  bool ok = mhz == ESP.getCpuFreqMHz() || system_update_cpu_freq(mhz);
  guardEnd();
  Json j;
  j.flag("ok", ok).num("cpu_mhz", ESP.getCpuFreqMHz()).send();
}

static void handleBenchMem() {
  guardBegin("bench_mem");
  Json j;
  j.num("heap_free", ESP.getFreeHeap()).num("heap_max_block", ESP.getMaxFreeBlockSize());
  // Buffers relevant to a 240x240 RGB565 panel.
  struct { const char *name; size_t size; } tries[] = {
      {"fb_1bit_7200", 7200},     {"strip_240x20_9600", 9600}, {"strip_240x60_28800", 28800},
      {"fb_8bit_57600", 57600},   {"fb_16bit_115200", 115200},
  };
  for (auto &t : tries) {
    void *p = malloc(t.size);
    j.flag(t.name, p != nullptr);
    free(p);
  }
  guardEnd();
  j.send();
}

static void handleBenchFlash() {
  guardBegin("bench_flash");
  const uint32_t BUF = 4096, total = 256 * 1024;
  uint32_t *buf = (uint32_t *)malloc(BUF);
  if (!buf) { guardEnd(); server.send(503, F("text/plain"), F("out of memory")); return; }
  uint32_t t = micros();
  for (uint32_t a = 0; a < total; a += BUF) {
    ESP.flashRead(a, buf, BUF);
  }
  uint32_t us = micros() - t;
  free(buf);
  guardEnd();
  Json j;
  j.num("read_bytes", total).num("read_us", us).num("read_kbs", us ? (uint64_t)total * 1000000 / 1024 / us : 0).send();
}

// ---------- network benchmarks ----------

// GET /api/bench/zero?len=N  -> N zero bytes from RAM (download speed without flash cost)
static void handleZero() {
  uint32_t len = strtoul(server.arg("len").c_str(), nullptr, 0);
  if (len == 0 || len > 8 * 1024 * 1024) len = 1024 * 1024;
  const size_t Z = 1460;
  uint8_t *z = (uint8_t *)calloc(1, Z);
  if (!z) { server.send(503, F("text/plain"), F("out of memory")); return; }
  WiFiClient c = server.client();
  server.setContentLength(len);
  server.send(200, F("application/octet-stream"), "");
  uint32_t left = len, t = millis();
  while (left && c.connected()) {
    size_t w = c.write(z, min<uint32_t>(left, Z));
    if (!w) { delay(1); continue; }
    left -= w;
    yield();
  }
  free(z);
  logf("zero: %u bytes in %u ms", len - left, millis() - t);
}

// POST /api/bench/sink (multipart) -> discard body, report receive speed
static uint32_t sinkBytes = 0, sinkStart = 0, sinkUs = 0;
static void handleSinkDone() {
  Json j;
  j.num("bytes", sinkBytes).num("us", sinkUs)
      .num("kbs", sinkUs ? (uint64_t)sinkBytes * 1000000 / 1024 / sinkUs : 0).send();
}
static void handleSinkUpload() {
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) { sinkBytes = 0; sinkStart = micros(); }
  else if (u.status == UPLOAD_FILE_WRITE) sinkBytes += u.currentSize;
  else if (u.status == UPLOAD_FILE_END) sinkUs = micros() - sinkStart;
}

#ifdef DIAG_TLS
// GET /api/bench/tls?host=example.com[&small=1] -> HTTPS handshake cost
static void handleTls() {
  String host = server.hasArg("host") ? server.arg("host") : String("www.google.com");
  bool small = server.hasArg("small");
  guardBegin("bench_tls");
  uint32_t heap0 = ESP.getFreeHeap();
  Json j;
  j.str("host", host).flag("small_buffers", small).num("heap_before", heap0);
  {
    BearSSL::WiFiClientSecure c;
    c.setInsecure();  // measuring cost only; no certificate validation
    if (small) c.setBufferSizes(4096, 512);
    uint32_t t = millis();
    bool ok = c.connect(host.c_str(), 443);
    j.flag("connected", ok).num("handshake_ms", millis() - t)
        .num("heap_connected", ESP.getFreeHeap()).num("heap_max_block", ESP.getMaxFreeBlockSize());
    if (ok) {
      c.print(String("GET / HTTP/1.1\r\nHost: ") + host + "\r\nConnection: close\r\n\r\n");
      uint32_t t0 = millis(), first = 0, bytes = 0;
      uint8_t b[512];
      while ((c.connected() || c.available()) && millis() - t0 < 8000 && bytes < 65536) {
        int n = c.read(b, sizeof(b));
        if (n > 0) { if (!bytes) first = millis() - t0; bytes += n; }
        else delay(2);
      }
      j.num("ttfb_ms", first).num("bytes", bytes).num("read_ms", millis() - t0);
    } else {
      char err[64];
      c.getLastSSLError(err, sizeof(err));
      j.str("error", err);
    }
    c.stop();
  }
  j.num("heap_after", ESP.getFreeHeap());
  guardEnd();
  j.send();
}
#endif

// ---------- Wi-Fi scan ----------

static void handleScan() {
  guardBegin("wifi_scan");
  int n = WiFi.scanNetworks(false, true);
  String a = "[";
  for (int i = 0; i < n; i++) {
    if (i) a += ',';
    Json j;
    j.str("ssid", WiFi.SSID(i)).num("rssi", WiFi.RSSI(i)).num("ch", WiFi.channel(i))
        .num("enc", WiFi.encryptionType(i));
    a += j.done();
  }
  a += ']';
  WiFi.scanDelete();
  guardEnd();
  server.send(200, F("application/json"), a);
}

// ---------- inputs (find the button) ----------
// Read-only: pins are never reconfigured, only sampled.

struct PinWatch {
  uint8_t pin;
  int last;
  uint32_t changes;
};
static PinWatch pins[] = {{4, -1, 0}, {12, -1, 0}, {16, -1, 0}};
static int a0Last = -1, a0Min = 1024, a0Max = -1;
static uint32_t lastPinPoll = 0, lastA0Poll = 0;

void diagLoop() {
  uint32_t now = millis();
  if (now - lastPinPoll >= 10) {
    lastPinPoll = now;
    for (auto &p : pins) {
      int v = digitalRead(p.pin);
      if (p.last >= 0 && v != p.last) p.changes++;
      p.last = v;
    }
  }
  if (now - lastA0Poll >= 200) {  // analogRead too often disturbs Wi-Fi
    lastA0Poll = now;
    a0Last = analogRead(A0);
    a0Min = min(a0Min, a0Last);
    a0Max = max(a0Max, a0Last);
  }
}

static void handleInputs() {
  if (server.hasArg("reset")) {
    for (auto &p : pins) p.changes = 0;
    a0Min = 1024; a0Max = -1;
  }
  Json j;
  for (auto &p : pins) {
    Json pj;
    pj.num("level", p.last).num("changes", p.changes);
    j.raw(("gpio" + String(p.pin)).c_str(), pj.done());
  }
  Json a;
  a.num("now", a0Last).num("min", a0Min).num("max", a0Max);
  j.raw("a0", a.done()).send();
}

static void handleReboot() {
  if (!requireAuth()) return;
  server.send(200, F("text/plain"), F("rebooting"));
  delay(200);
  ESP.restart();
}

void diagRegister() {
  server.on("/api/info", HTTP_GET, handleInfo);
  server.on("/api/log", HTTP_GET, handleLog);
  server.on("/api/bench/cpu", HTTP_GET, handleBenchCpu);
  server.on("/api/cpu", HTTP_GET, handleCpuFreq);
  server.on("/api/bench/mem", HTTP_GET, handleBenchMem);
  server.on("/api/bench/flash", HTTP_GET, handleBenchFlash);
  server.on("/api/bench/zero", HTTP_GET, handleZero);
  server.on("/api/bench/sink", HTTP_POST, handleSinkDone, handleSinkUpload);
#ifdef DIAG_TLS
  server.on("/api/bench/tls", HTTP_GET, handleTls);
#else
  server.on("/api/bench/tls", HTTP_GET, [] { server.send(501, F("text/plain"), F("TLS bench not in this build (DIAG_TLS)")); });
#endif
  server.on("/api/wifi/scan", HTTP_GET, handleScan);
  server.on("/api/inputs", HTTP_GET, handleInputs);
  server.on("/api/reboot", HTTP_POST, handleReboot);
}
