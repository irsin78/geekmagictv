// Crash guard (RTC memory) and a small in-RAM event log.

#include <stdarg.h>

#include "common.h"

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

void logRegister() {
  server.on("/api/log", HTTP_GET, [] {
    String out;
    out.reserve(logCount * 64);
    for (int i = 0; i < logCount; i++) {
      out += logBuf[(logHead - logCount + i + LOG_LINES) % LOG_LINES];
      out += '\n';
    }
    server.send(200, F("text/plain"), out);
  });
}
