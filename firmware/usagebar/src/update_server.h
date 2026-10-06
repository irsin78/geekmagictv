#pragma once
// HTTP firmware update (/update) - the device's only OTA path.
//
// Adapted from the ESP8266 Arduino core's ESP8266HTTPUpdateServer (3.1.2, LGPL-2.1,
// https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266HTTPUpdateServer) with two changes:
//   1. authentication is the optional admin password of the web page (passwordOk(): no password
//      set = open; otherwise the session cookie from POST /api/login), not HTTP basic auth;
//   2. "filesystem" uploads are refused, so a wrong click can never erase the stock LittleFS.
// The firmware upload path itself (Update.begin/write/end) is unchanged.

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <StreamString.h>
#include <Updater.h>

#include "common.h"

class PasswordUpdateServer {
 public:
  void setup(ESP8266WebServer *server, const char *path) {
    _server = server;

    // handler for the /update form page
    _server->on(path, HTTP_GET, [&]() {
      if (!passwordOk()) {  // log in on the main page first (password only, no user name)
        _server->sendHeader(F("Location"), F("/?login"));
        return _server->send(302);
      }
      _server->send_P(200, PSTR("text/html"), FORM);
    });

    // handler for the /update form POST (once file upload finishes)
    _server->on(path, HTTP_POST, [&]() {
      if (!_authenticated) return _server->send(401, F("text/plain"), F("admin password required: log in on the main page"));
      if (Update.hasError() || _updaterError.length()) {
        _server->send(200, F("text/html"), String(F("Update error: ")) + _updaterError);
      } else {
        _server->client().setNoDelay(true);
        _server->send_P(200, PSTR("text/html"), SUCCESS);
        delay(100);
        _server->client().stop();
        ESP.restart();
      }
    }, [&]() {
      // handler for the file upload, gets the sketch bytes, and writes them through Update
      HTTPUpload &upload = _server->upload();

      if (upload.status == UPLOAD_FILE_START) {
        _updaterError.clear();
        _authenticated = passwordOk();
        if (!_authenticated) return;
        if (upload.name == "filesystem") {
          _updaterError = F("filesystem updates are disabled (they would erase the stock LittleFS)");
          return;
        }
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace, U_FLASH)) {  // start with max available size
          _setUpdaterError();
        }
      } else if (_authenticated && upload.status == UPLOAD_FILE_WRITE && !_updaterError.length()) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          _setUpdaterError();
        }
      } else if (_authenticated && upload.status == UPLOAD_FILE_END && !_updaterError.length()) {
        if (!Update.end(true)) {  // true to set the size to the current progress
          _setUpdaterError();
        }
      } else if (_authenticated && upload.status == UPLOAD_FILE_ABORTED) {
        Update.end();
      }
      esp_yield();
    });
  }

 private:
  void _setUpdaterError() {
    StreamString str;
    Update.printError(str);
    _updaterError = str.c_str();
  }

  static constexpr const char FORM[] PROGMEM =
      "<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'/></head><body>"
      "<form method='POST' action='' enctype='multipart/form-data'>Firmware:<br>"
      "<input type='file' accept='.bin,.bin.gz' name='firmware'> "
      "<input type='submit' value='Update Firmware'></form></body></html>";
  static constexpr const char SUCCESS[] PROGMEM =
      "<META http-equiv=\"refresh\" content=\"15;URL=/\">Update Success! Rebooting...";

  ESP8266WebServer *_server = nullptr;
  bool _authenticated = false;
  String _updaterError;
};
