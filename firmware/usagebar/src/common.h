#pragma once
#include <Arduino.h>
#include <ESP8266WebServer.h>

extern ESP8266WebServer server;
extern bool apMode;


// Crash guard: remembers (in RTC memory) which task was running if the chip resets mid-task.
void guardInit();
void guardBegin(const char *task);
void guardEnd();
const char *lastCrashedTask();

void logf(const char *fmt, ...);
void logRegister();

// ---- persistent settings (settings.cpp) ----

// Stored in EEPROM: only append new languages, never reorder.
enum Lang : uint8_t {
  LANG_KO, LANG_EN, LANG_JA, LANG_ZH, LANG_ES, LANG_PT, LANG_FR, LANG_DE, LANG_IT,
  LANG_ZH_TW, LANG_RU, LANG_UK, LANG_PL, LANG_NL, LANG_TR, LANG_VI, LANG_ID, LANG_TH, LANG_AR,
  LANG_COUNT
};

struct Settings {
  uint32_t magic;
  uint8_t backlight;  // 0..100
  uint8_t lang;       // Lang: unit words of the reset countdown + web page language
  char city[48];      // weather place shown on the web page (UTF-8)
  float lat, lon;     // weather place, read by the Mac's collector
  char adminPass[33]; // optional admin password, "" = protection off
  char wifiSsid[33];  // Wi-Fi set on the web page ("" = use the one compiled in, if any)
  char wifiPass[65];
  char pushToken[25]; // generated on first boot; used when the build has no PUSH_TOKEN
};

extern Settings settings;
void settingsLoad();
void settingsRegister();
const char *wifiSsid();  // web-page Wi-Fi, else the compiled-in one
const char *wifiPass();
bool wifiConfigured();   // false: the device runs the open setup access point
const char *pushToken(); // bearer token expected on POST /api/usage
bool passwordOk();   // true when no admin password is set or this request is logged in
bool requireAuth();  // passwordOk(), else answers 401 {"error":"login"}

// ---- usage data (pushed by the Mac) ----

struct UsageWindow {
  char label[6];     // "5h", "7d", "Pro"...
  int8_t used;       // 0..100, -1 = unknown
  uint32_t resetAt;  // unix seconds, 0 = unknown
};

struct UsageProvider {
  char id[12];
  char name[12];
  char plan[14];
  char error[32];     // failure reason while the numbers below are the last good ones
  uint32_t fetchedAt;  // unix time of the last successful fetch, 0 = unknown
  uint8_t windows;
  UsageWindow win[3];  // Claude Max: 5h, 7d, Fable 7d
};

struct UsageState {
  bool have;
  uint32_t generatedAt;  // unix seconds from the pusher
  uint32_t receivedMs;   // millis() when received
  char net[8];           // Mac's internet: "ok" or "login" (network login expired)
  uint32_t netSince;     // unix time the internet went away, 0 = unknown
  char wxIcon[16];       // weather icon name ("clear_day", "rain", ...), "" = none
  int16_t wxTemp10;      // temperature in 0.1 degC
  uint8_t count;
  UsageProvider p[3];
};

extern UsageState usage;
void usageRegister();
uint32_t nowUnix();  // NTP time if synced, else estimated from the last push, else 0

// ---- display ----
void backlightSet(uint8_t pct);
void displayBoot();
void displayLoop();
void displayRequestRedraw();
