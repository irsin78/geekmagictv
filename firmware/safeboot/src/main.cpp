// SmallTV safeboot: Wi-Fi + OTA + read-only flash dump + on-demand diagnostics.
//
// Boot order is deliberate: Wi-Fi and OTA come up first, the display last.
// Every diagnostic runs only when requested over HTTP, inside a crash guard.
//
// Endpoints (see diag.cpp / display.cpp for /api/*)
//   GET  /            -> redirect to /diag
//   GET  /diag        diagnostics dashboard
//   GET  /flash.bin   full raw flash image (auth)
//   GET  /flash?offset=<n>&len=<n>   raw flash range (auth)
//   GET/POST /update  firmware upload (auth)
// Plus ArduinoOTA on port 8266 (password = ADMIN_PASS).

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#ifndef SAFEBOOT_LEAN
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#endif

#include "diag.h"
#include "page.h"
#include "secrets.h"

static const char *HOSTNAME = "smalltv-safe";
static const uint32_t STA_TIMEOUT_MS = 30000;
static const uint32_t STA_RETRY_MS = 120000;

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;

bool apMode = false;
static uint32_t lastStaAttempt = 0;

bool requireAuth() {
  if (server.authenticate(ADMIN_USER, ADMIN_PASS)) return true;
  server.requestAuthentication();
  return false;
}

static void startAp() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  apMode = true;
  logf("AP mode: %s @ %s", AP_SSID, WiFi.softAPIP().toString().c_str());
}

static void connectWifi() {
  WiFi.persistent(false);  // don't rewrite the SDK config sector
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lastStaAttempt = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - lastStaAttempt < STA_TIMEOUT_MS) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    logf("STA connected: %s (%d dBm)", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    startAp();
  }
}

// Stream [offset, offset+len) of physical flash. Offset/len must be 4-byte aligned.
static void streamFlash(uint32_t offset, uint32_t len) {
  const uint32_t BUF = 4096;
  uint32_t *buf = (uint32_t *)malloc(BUF);  // word aligned, as spi_flash_read requires
  if (!buf) { server.send(503, F("text/plain"), F("out of memory")); return; }
  WiFiClient client = server.client();
  server.setContentLength(len);
  server.sendHeader(F("Content-Disposition"), F("attachment; filename=flash.bin"));
  server.send(200, F("application/octet-stream"), "");

  uint32_t start = millis();
  uint32_t end = offset + len;
  uint32_t addr = offset;
  while (addr < end && client.connected()) {
    uint32_t n = min<uint32_t>(BUF, end - addr);
    if (!ESP.flashRead(addr, buf, n)) break;
    size_t sent = 0;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(buf);
    while (sent < n && client.connected()) {
      size_t w = client.write(p + sent, n - sent);
      if (w == 0) { delay(1); continue; }
      sent += w;
    }
    addr += n;
    yield();
  }
  free(buf);
  logf("flash read 0x%x+%u: %u bytes in %u ms", offset, len, addr - offset, millis() - start);
}

static void handleFlashAll() {
  if (!requireAuth()) return;
  streamFlash(0, ESP.getFlashChipRealSize());
}

static void handleFlashRange() {
  if (!requireAuth()) return;
  uint32_t size = ESP.getFlashChipRealSize();
  uint32_t offset = strtoul(server.arg("offset").c_str(), nullptr, 0);
  uint32_t len = strtoul(server.arg("len").c_str(), nullptr, 0);
  if (len == 0 || offset >= size || len > size - offset || ((offset | len) & 3)) {
    server.send(400, F("text/plain"), F("bad offset/len (must be 4-byte aligned, within flash)"));
    return;
  }
  streamFlash(offset, len);
}

void setup() {
  Serial.begin(115200);
  guardInit();
  logf("safeboot " SAFEBOOT_VERSION " reset=%s crashed_task=%s",
       ESP.getResetReason().c_str(), lastCrashedTask());

  // Backlight on early so the device doesn't look dead (panel is initialised last).
  pinMode(5, OUTPUT);
  digitalWrite(5, LOW);  // active low

  connectWifi();

#ifndef SAFEBOOT_LEAN  // lean builds keep only HTTP /update for recovery
  MDNS.begin(HOSTNAME);
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(ADMIN_PASS);
  ArduinoOTA.begin();
#endif

  updater.setup(&server, "/update", ADMIN_USER, ADMIN_PASS);
  server.on("/", HTTP_GET, [] {
    server.sendHeader(F("Location"), F("/diag"));
    server.send(302);
  });
  server.on("/diag", HTTP_GET, [] { server.send_P(200, "text/html", DIAG_PAGE); });
  server.on("/flash.bin", HTTP_GET, handleFlashAll);
  server.on("/flash", HTTP_GET, handleFlashRange);
  diagRegister();
  displayRegister();
  server.begin();
#ifndef SAFEBOOT_LEAN
  MDNS.addService("http", "tcp", 80);
#endif
  logf("OTA + web ready");

  // Last: the display. If a previous boot died in a display task, skip it this time.
  if (strncmp(lastCrashedTask(), "disp", 4) == 0) {
    logf("display skipped: previous boot crashed in %s", lastCrashedTask());
  } else {
    displayBoot();
  }
}

void loop() {
  server.handleClient();
#ifndef SAFEBOOT_LEAN
  ArduinoOTA.handle();
  MDNS.update();
#endif
  diagLoop();

  // In AP fallback, keep retrying the home network in the background.
  if (apMode && WiFi.status() != WL_CONNECTED && millis() - lastStaAttempt > STA_RETRY_MS) {
    lastStaAttempt = millis();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
  if (apMode && WiFi.status() == WL_CONNECTED) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apMode = false;
    logf("STA connected: %s", WiFi.localIP().toString().c_str());
  }
}
