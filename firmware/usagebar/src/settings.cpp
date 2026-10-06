// Persistent settings (EEPROM sector 0x3FB000, unused by the stock firmware), their API and the
// optional admin password.
//
//   GET  /api/settings   {"bl":100,"lang":"ko","city":"Seoul","lat":37.57,"lon":126.98,"locked":false,"authed":true}
//   POST /api/settings   JSON with any of bl/lang/city+lat+lon/pass (pass "" turns protection off);
//                        saved only when something changed. Needs the password when one is set.
//   POST /api/login      {"pass":"..."} -> session cookie (password only, no user name)
//   POST /api/logout
//
// The password protects settings, reboot and /update. It is off until one is set on the web page.
// Sessions live in RAM: a reboot logs everyone out. Scripts may send "X-Password: ..." instead.

#include <ArduinoJson.h>
#include <EEPROM.h>

#include "common.h"

static const uint32_t MAGIC_V1 = 0x55534231;  // "USB1": {magic, backlight}
static const uint32_t MAGIC_V2 = 0x55534232;  // "USB2": SettingsV2
static const uint32_t MAGIC_V3 = 0x55534233;  // "USB3": Settings

struct SettingsV2 {
  uint32_t magic;
  uint8_t backlight, lang;
  char city[48];
  float lat, lon;
};

Settings settings{MAGIC_V3, 100, LANG_KO, "Seoul", 37.5665f, 126.9780f, ""};

static const char *const LANG_CODES[LANG_COUNT] = {"ko", "en", "ja", "zh", "es", "pt", "fr", "de", "it", "zh-TW",
                                                   "ru", "uk", "pl", "nl", "tr", "vi", "id", "th", "ar"};
static char session[17] = "";  // current login token, "" = nobody logged in

void settingsLoad() {
  EEPROM.begin(sizeof(Settings));
  Settings s;
  EEPROM.get(0, s);
  if (s.magic == MAGIC_V3 && s.backlight <= 100 && s.lang < LANG_COUNT) {
    s.city[sizeof(s.city) - 1] = 0;
    s.adminPass[sizeof(s.adminPass) - 1] = 0;
    settings = s;
  } else if (s.magic == MAGIC_V2) {  // upgrade from 2.x: same fields, no password yet
    SettingsV2 v2;
    EEPROM.get(0, v2);
    if (v2.backlight <= 100 && v2.lang < LANG_COUNT) {
      settings.backlight = v2.backlight;
      settings.lang = v2.lang;
      memcpy(settings.city, v2.city, sizeof(settings.city));
      settings.city[sizeof(settings.city) - 1] = 0;
      settings.lat = v2.lat;
      settings.lon = v2.lon;
    }
  } else if (s.magic == MAGIC_V1 && s.backlight <= 100) {
    settings.backlight = s.backlight;  // upgrade from 1.x: keep the brightness, default the rest
  }
}

static void settingsSave() {
  EEPROM.put(0, settings);
  EEPROM.commit();
}

// ---------- admin password ----------

bool passwordOk() {
  if (!settings.adminPass[0]) return true;  // protection off
  if (server.hasHeader("X-Password") && server.header("X-Password") == settings.adminPass) return true;
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
  doc["authed"] = passwordOk();
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

void settingsRegister() {
  server.on("/api/settings", HTTP_GET, handleGet);
  server.on("/api/settings", HTTP_POST, handlePost);
  server.on("/api/login", HTTP_POST, handleLogin);
  server.on("/api/logout", HTTP_POST, handleLogout);
}
