// Usage state: accepts compact JSON pushed by tools/push_usage.py.
//
// POST /api/usage  (Authorization: Bearer <push token>, see pushToken())
// {
//   "ts": 1790000000,                       // unix seconds when collected
//   "net": "ok", "net_since": 0,            // "login" while the Mac's network login has expired
//   "wx": {"icon": "clear_day", "t": 18.2}, // current weather (Open-Meteo), optional
//   "tz_off": 32400,                        // UTC offset of the weather place (clock time zone)
//   "p": [                                  // up to 3 providers, display order
//     {"id": "claude", "n": "Claude", "plan": "Max", "err": "", "at": 1790000000,
//      "w": [{"l": "5h", "u": 28, "r": 1790003600},   // label, used %, reset unix
//            {"l": "7d", "u": 61, "r": 1790500000}]}
//   ]
// }

#include <ArduinoJson.h>
#include <time.h>

#include "common.h"

UsageState usage{};

uint32_t nowUnix() {
  time_t t = time(nullptr);
  if (t > 1700000000) return (uint32_t)t;
  if (usage.have && usage.generatedAt) return usage.generatedAt + (millis() - usage.receivedMs) / 1000;
  return 0;
}

// Clock time zone follows the weather place: the collector sends its UTC offset ("tz_off", seconds).
static long appliedTzOff = 9 * 3600;  // configTime("KST-9") at boot

static void applyTz(long off) {
  if (off == appliedTzOff || off < -14 * 3600 || off > 14 * 3600) return;
  char tz[20];
  const long a = labs(off);
  snprintf(tz, sizeof(tz), "UTC%c%ld:%02ld", off >= 0 ? '-' : '+', a / 3600, (a % 3600) / 60);  // POSIX sign is inverted
  setenv("TZ", tz, 1);
  tzset();
  appliedTzOff = off;
  logf("time zone %s", tz);
}

static bool pushAuthorized() {
  return server.header("Authorization") == String("Bearer ") + pushToken();
}

static void handlePush() {
  if (!pushAuthorized()) {
    server.send(401, F("application/json"), F("{\"error\":\"unauthorized\"}"));
    return;
  }
  if (!strcmp(lastCrashedTask(), "usage_parse")) {
    // The previous boot died parsing a push; the Mac would resend the same payload every minute.
    server.send(503, F("application/json"), F("{\"error\":\"previous push crashed the parser\"}"));
    return;
  }
  guardBegin("usage_parse");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    guardEnd();
    server.send(400, F("application/json"), String("{\"error\":\"") + err.c_str() + "\"}");
    return;
  }

  UsageState next{};
  next.generatedAt = doc["ts"] | 0u;
  strlcpy(next.net, doc["net"] | "ok", sizeof(next.net));
  next.netSince = doc["net_since"] | 0u;
  strlcpy(next.wxIcon, doc["wx"]["icon"] | "", sizeof(next.wxIcon));
  next.wxTemp10 = (int16_t)lroundf((doc["wx"]["t"] | 0.0f) * 10);
  JsonArray providers = doc["p"];
  for (JsonObject src : providers) {
    if (next.count >= 3) break;
    UsageProvider &p = next.p[next.count++];
    strlcpy(p.id, src["id"] | "", sizeof(p.id));
    strlcpy(p.name, src["n"] | "?", sizeof(p.name));
    strlcpy(p.plan, src["plan"] | "", sizeof(p.plan));
    strlcpy(p.error, src["err"] | "", sizeof(p.error));
    p.fetchedAt = src["at"] | 0u;
    for (JsonObject w : src["w"].as<JsonArray>()) {
      if (p.windows >= 2) break;
      UsageWindow &x = p.win[p.windows++];
      strlcpy(x.label, w["l"] | "", sizeof(x.label));
      x.used = w["u"].isNull() ? -1 : (int8_t)constrain((int)lroundf(w["u"].as<float>()), 0, 100);
      x.resetAt = w["r"] | 0u;
    }
  }
  guardEnd();

  next.have = true;
  next.receivedMs = millis();
  if (doc["tz_off"].is<long>()) applyTz(doc["tz_off"].as<long>());
  next.seq = usage.seq + 1;
  usage = next;
  displayRequestRedraw();
  logf("usage push: %u providers", usage.count);
  server.send(200, F("application/json"), String("{\"ok\":true,\"seq\":") + usage.seq + "}");
}

static void handleGet() {
  JsonDocument doc;
  doc["have"] = usage.have;
  doc["ts"] = usage.generatedAt;
  doc["age_s"] = usage.have ? (millis() - usage.receivedMs) / 1000 : 0;
  doc["now"] = nowUnix();
  doc["net"] = usage.net;
  doc["net_since"] = usage.netSince;
  doc["wx"]["icon"] = usage.wxIcon;
  doc["wx"]["t"] = usage.wxTemp10 / 10.0;
  JsonArray ps = doc["p"].to<JsonArray>();
  for (uint8_t i = 0; i < usage.count; i++) {
    const UsageProvider &p = usage.p[i];
    JsonObject o = ps.add<JsonObject>();
    o["id"] = p.id;
    o["n"] = p.name;
    o["plan"] = p.plan;
    o["err"] = p.error;
    o["at"] = p.fetchedAt;
    JsonArray ws = o["w"].to<JsonArray>();
    for (uint8_t w = 0; w < p.windows; w++) {
      JsonObject x = ws.add<JsonObject>();
      x["l"] = p.win[w].label;
      x["u"] = p.win[w].used;
      x["r"] = p.win[w].resetAt;
    }
  }
  String out;
  serializeJson(doc, out);
  server.send(200, F("application/json"), out);
}

void usageRegister() {
  server.on("/api/usage", HTTP_POST, handlePush);
  server.on("/api/usage", HTTP_GET, handleGet);
}
