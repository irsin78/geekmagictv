// SmallTV usagebar: Claude / Codex / Antigravity usage display.
//
// Boot order is deliberate: Wi-Fi and HTTP /update come up first, the display last,
// so a display bug can never take away OTA recovery.
//
// Endpoints
//   GET  /              status + settings page (page.h)
//   GET  /api/usage     current usage state (JSON)
//   POST /api/usage     push new usage (Authorization: Bearer PUSH_TOKEN)
//   GET/POST /api/settings   brightness, language, weather place, admin password (settings.cpp)
//   POST /api/login, /api/logout   optional admin password (password only, session cookie)
//   GET  /api/info, /api/log     diagnostics
//   POST /api/reboot    (admin password if set)
//   GET/POST /update    firmware upload (admin password if set) - the only OTA path (update_server.h);
//                       filesystem uploads are refused so the stock LittleFS can't be erased

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <time.h>

#include "common.h"
#include "page.h"
#include "update_server.h"
#include "secrets.h"

static const char *HOSTNAME = "smalltv-usage";
static const uint32_t STA_TIMEOUT_MS = 30000;
static const uint32_t STA_RETRY_MS = 120000;

ESP8266WebServer server(80);
PasswordUpdateServer updater;

bool apMode = false;
static uint32_t lastStaAttempt = 0;

// ---------- Wi-Fi ----------

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
  while (WiFi.status() != WL_CONNECTED && millis() - lastStaAttempt < STA_TIMEOUT_MS) delay(250);
  if (WiFi.status() == WL_CONNECTED) {
    logf("STA connected: %s (%d dBm)", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    startAp();
  }
}

// ---------- HTTP handlers ----------

static void handleInfo() {
  rst_info *ri = ESP.getResetInfoPtr();
  char buf[512];
  snprintf(buf, sizeof(buf),
           "{\"usagebar\":\"%s\",\"core\":\"%s\",\"cpu_mhz\":%u,\"sketch\":%u,\"free_ota\":%u,"
           "\"heap_free\":%u,\"heap_max_block\":%u,\"reset\":\"%s\",\"exc_cause\":%u,\"exc_epc1\":\"0x%x\","
           "\"crashed_task\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"ntp\":%s,\"uptime_s\":%lu}",
           USAGEBAR_VERSION, ESP.getCoreVersion().c_str(), ESP.getCpuFreqMHz(), ESP.getSketchSize(),
           ESP.getFreeSketchSpace(), ESP.getFreeHeap(), ESP.getMaxFreeBlockSize(),
           ESP.getResetReason().c_str(), ri->exccause, ri->epc1, lastCrashedTask(),
           (apMode ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str(), WiFi.RSSI(),
           time(nullptr) > 1700000000 ? "true" : "false", millis() / 1000);
  server.send(200, F("application/json"), buf);
}

static void handleReboot() {
  if (!requireAuth()) return;
  server.send(200, F("text/plain"), F("rebooting"));
  delay(200);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  guardInit();
  logf("usagebar " USAGEBAR_VERSION " reset=%s crashed_task=%s", ESP.getResetReason().c_str(), lastCrashedTask());
  settingsLoad();

  pinMode(5, OUTPUT);
  digitalWrite(5, LOW);  // backlight on (active low) so the device doesn't look dead

  connectWifi();
  // Korea until the first push brings the weather place's UTC offset (usage.cpp applies it).
  configTime("KST-9", "pool.ntp.org", "time.google.com");

  updater.setup(&server, "/update");  // protected only when an admin password is set on the web page
  server.collectHeaders("Authorization", "Cookie", "X-Password");
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", ROOT_PAGE); });
  server.on("/api/info", HTTP_GET, handleInfo);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  logRegister();
  settingsRegister();
  usageRegister();
  server.begin();
  logf("OTA + web ready");

  // Last: the display. If a previous boot died in a display task, skip it this time.
  if (strncmp(lastCrashedTask(), "disp", 4) == 0) {
    logf("display skipped: previous boot crashed in %s", lastCrashedTask());
  } else {
    displayBoot();
    backlightSet(settings.backlight);
  }
}

void loop() {
  server.handleClient();
  displayLoop();

  // While someone is connected to the fallback AP (e.g. to OTA), don't retry the home network:
  // the reconnect scan changes channel and drops AP clients.
  if (apMode && WiFi.status() != WL_CONNECTED && WiFi.softAPgetStationNum() == 0 &&
      millis() - lastStaAttempt > STA_RETRY_MS) {
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
