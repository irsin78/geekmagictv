#pragma once
#include <Arduino.h>
#include <ESP8266WebServer.h>

extern ESP8266WebServer server;
extern bool apMode;

bool requireAuth();

// Crash guard: remembers (in RTC memory) which task was running if the chip resets mid-task.
void guardInit();
void guardBegin(const char *task);
void guardEnd();
const char *lastCrashedTask();  // "" if the previous boot ended cleanly

// Small in-RAM event log.
void logf(const char *fmt, ...);

// Minimal JSON object builder.
class Json {
 public:
  Json() { s.reserve(1024); s = '{'; }
  Json &str(const char *k, const String &v) {
    key(k);
    s += '"';
    for (char c : v) {
      if (c == '"' || c == '\\') s += '\\';
      if ((uint8_t)c >= 0x20) s += c;
    }
    s += '"';
    return *this;
  }
  template <typename T> Json &num(const char *k, T v) { key(k); s += String(v); return *this; }
  Json &flag(const char *k, bool v) { key(k); s += v ? F("true") : F("false"); return *this; }
  Json &raw(const char *k, const String &json) { key(k); s += json; return *this; }
  String done() { s += '}'; return s; }
  void send() { server.send(200, F("application/json"), done()); }

 private:
  void key(const char *k) {
    if (s.length() > 1) s += ',';
    s += '"'; s += k; s += F("\":");
  }
  String s;
};

void diagRegister();
void diagLoop();
String sysInfoJson();

void backlightSet(uint8_t pct);
void displayBoot();
void displayRegister();
