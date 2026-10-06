// Persistent settings (EEPROM sector 0x3FB000, unused by the stock firmware), their API, Wi-Fi
// setup and the optional admin password.
//
//   GET  /api/settings   {"bl","lang","city","lat","lon","locked","authed","wifi","setup","token"}
//   POST /api/settings   JSON with any of bl/lang/city+lat+lon/pass (pass "" turns protection off);
//                        saved only when something changed
//   GET  /api/wifi/scan  nearby networks
//   POST /api/wifi       {"ssid","pass"}: save and reboot into that network
//   POST /api/login      {"pass"} -> session cookie (password only, no user name)
//   POST /api/logout
//
// The admin password protects settings, Wi-Fi, reboot and /update. It is off until one is set on the
// web page. Sessions live in RAM: a reboot logs everyone out.
// "token" (the push token for tools/push_usage.py) is only returned to an authorised request.

#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>

#include "common.h"
#include "config.h"

static const uint32_t MAGIC = 0x55534234;  // "USB4"; anything else in EEPROM -> defaults

Settings settings{MAGIC, 100, LANG_KO, "Seoul", 37.5665f, 126.9780f, "", "", "", ""};

static const char *const LANG_CODES[LANG_COUNT] = {"ko", "en", "ja", "zh", "es", "pt", "fr", "de", "it", "zh-TW",
                                                   "ru", "uk", "pl", "nl", "tr", "vi", "id", "th", "ar"};
static char session[17] = "";  // current login token, "" = nobody logged in

static void settingsSave() {
  EEPROM.put(0, settings);
  EEPROM.commit();
}

void settingsLoad() {
  EEPROM.begin(sizeof(Settings));
  Settings s;
  EEPROM.get(0, s);
  if (s.magic == MAGIC && s.backlight <= 100 && s.lang < LANG_COUNT) {
    s.city[sizeof(s.city) - 1] = 0;
    s.adminPass[sizeof(s.adminPass) - 1] = 0;
    s.wifiSsid[sizeof(s.wifiSsid) - 1] = 0;
    s.wifiPass[sizeof(s.wifiPass) - 1] = 0;
    s.pushToken[sizeof(s.pushToken) - 1] = 0;
    settings = s;
  }
  if (!settings.pushToken[0]) {  // first boot: make this device's push token
    snprintf(settings.pushToken, sizeof(settings.pushToken), "%08x%08x%08x", ESP.random(), ESP.random(), ESP.random());
    settingsSave();
  }
}

// ---------- Wi-Fi / push token from settings or the build ----------

const char *wifiSsid() { return settings.wifiSsid[0] ? settings.wifiSsid : WIFI_SSID; }
const char *wifiPass() { return settings.wifiSsid[0] ? settings.wifiPass : WIFI_PASS; }
bool wifiConfigured() { return wifiSsid()[0] != 0; }

const char *pushToken() {
#ifdef PUSH_TOKEN
  return PUSH_TOKEN;  // your own build: the token in secrets.h (tools/push_usage.py reads it there)
#else
  return settings.pushToken;
#endif
}

// ---------- admin password ----------

bool passwordOk() {
  if (!settings.adminPass[0]) return true;  // protection off
  return session[0] && server.header("Cookie").indexOf(String("smalltv=") + session) >= 0;
}

bool requireAuth() {
  if (passwordOk()) return true;
  server.send(401, F("application/json"), F("{\"error\":\"login\"}"));  // the page shows its password box
  return false;
}

static void newSession() {
  snprintf(session, sizeof(session), "%08x%08x", ESP.random(), ESP.random());
  server.sendHeader(F("Set-Cookie"), String("smalltv=") + session + "; Path=/; HttpOnly; SameSite=Strict");
}

static void handleLogin() {
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  const char *pass = doc["pass"] | "";
  if (settings.adminPass[0] && strcmp(pass, settings.adminPass)) {
    delay(1000);  // slow down guessing
    server.send(401, F("application/json"), F("{\"error\":\"wrong password\"}"));
    return;
  }
  newSession();
  server.send(200, F("application/json"), F("{\"ok\":true}"));
}

static void handleLogout() {
  session[0] = 0;
  server.sendHeader(F("Set-Cookie"), F("smalltv=; Path=/; Max-Age=0"));
  server.send(200, F("application/json"), F("{\"ok\":true}"));
}

// ---------- settings API ----------

static void handleGet() {
  JsonDocument doc;
  doc["bl"] = settings.backlight;
  doc["lang"] = LANG_CODES[settings.lang];
  doc["city"] = settings.city;
  doc["lat"] = settings.lat;
  doc["lon"] = settings.lon;
  doc["locked"] = settings.adminPass[0] != 0;  // the password itself is never sent
  const bool authed = passwordOk();
  doc["authed"] = authed;
  doc["wifi"] = wifiSsid();
  doc["setup"] = !wifiConfigured();
  if (authed) doc["token"] = pushToken();
  String out;
  serializeJson(doc, out);
  server.send(200, F("application/json"), out);
}

static void handlePost() {
  if (!requireAuth()) return;
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, F("application/json"), F("{\"error\":\"bad json\"}"));
    return;
  }
  Settings next = settings;
  if (doc["bl"].is<int>()) next.backlight = constrain(doc["bl"].as<int>(), 0, 100);
  if (const char *lang = doc["lang"]) {
    for (uint8_t i = 0; i < LANG_COUNT; i++)
      if (!strcmp(lang, LANG_CODES[i])) next.lang = i;
  }
  if (doc["lat"].is<float>() && doc["lon"].is<float>()) {
    const float lat = doc["lat"], lon = doc["lon"];
    if (lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180) {
      next.lat = lat;
      next.lon = lon;
      strlcpy(next.city, doc["city"] | "", sizeof(next.city));
    }
  }
  const bool passChanged = doc["pass"].is<const char *>();
  if (passChanged) strlcpy(next.adminPass, doc["pass"].as<const char *>(), sizeof(next.adminPass));

  if (memcmp(&next, &settings, sizeof(Settings))) {  // EEPROM commits wear the flash: only on change
    const bool blChanged = next.backlight != settings.backlight;
    settings = next;
    settingsSave();
    if (blChanged) backlightSet(settings.backlight);
    displayRequestRedraw();
    logf("settings saved: lang=%s city=%s lock=%s", LANG_CODES[settings.lang], settings.city,
         settings.adminPass[0] ? "on" : "off");
  }
  if (passChanged && settings.adminPass[0]) newSession();  // keep the browser that set it logged in
  handleGet();
}

// ---------- Wi-Fi setup ----------

static void handleScan() {
  const int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray list = doc.to<JsonArray>();
  for (int i = 0; i < n && list.size() < 20; i++) {
    const String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    bool dup = false;  // the same network on several access points: keep the strongest (listed first)
    for (JsonObject o : list) dup |= ssid == o["ssid"].as<const char *>();
    if (dup) continue;
    JsonObject o = list.add<JsonObject>();
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = WiFi.encryptionType(i) == ENC_TYPE_NONE;
  }
  WiFi.scanDelete();
  String out;
  serializeJson(doc, out);
  server.send(200, F("application/json"), out);
}

static void handleWifi() {
  if (!requireAuth()) return;
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  const char *ssid = doc["ssid"] | "";
  if (!ssid[0] || strlen(ssid) >= sizeof(settings.wifiSsid)) {
    server.send(400, F("application/json"), F("{\"error\":\"ssid\"}"));
    return;
  }
  strlcpy(settings.wifiSsid, ssid, sizeof(settings.wifiSsid));
  strlcpy(settings.wifiPass, doc["pass"] | "", sizeof(settings.wifiPass));
  settingsSave();
  logf("wifi saved: %s, rebooting", settings.wifiSsid);
  server.send(200, F("application/json"), F("{\"ok\":true}"));
  delay(500);
  ESP.restart();
}

void settingsRegister() {
  server.on("/api/settings", HTTP_GET, handleGet);
  server.on("/api/settings", HTTP_POST, handlePost);
  server.on("/api/wifi/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_POST, handleWifi);
  server.on("/api/login", HTTP_POST, handleLogin);
  server.on("/api/logout", HTTP_POST, handleLogout);
}
