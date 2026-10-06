#pragma once
// Build-time configuration. include/secrets.h is optional:
//  - with it (your own build): the Wi-Fi, fallback AP and push token in it are used as defaults;
//  - without it, or with `pio run -e release`: nothing secret is compiled in. The device then opens
//    the open "SmallTV-Setup" access point until Wi-Fi is set on its web page, and generates its
//    own push token (shown on the web page).
// Wi-Fi saved on the web page always wins over the compiled-in one.

#if !defined(USAGEBAR_RELEASE) && __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif
#ifndef AP_SSID
#define AP_SSID "SmallTV-Setup"
#endif
#ifndef AP_PASS
#define AP_PASS ""  // open: the AP only runs while the device has no working Wi-Fi
#endif
// PUSH_TOKEN: when not defined, a random token is generated on first boot (settings.cpp).
