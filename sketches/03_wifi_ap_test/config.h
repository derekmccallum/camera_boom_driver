// config.h - non-secret settings for the WiFi test.
// Network name and passwords live in secrets.h (gitignored).
#pragma once

constexpr int  PIN_LED = 2;

// Station mode: join the home network and take a DHCP address.
constexpr char HOSTNAME[]   = "boom";   // DHCP hostname; also http://boom.local via mDNS
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

// If the home network is not found (e.g. on location), start our own access
// point instead so the tablet can still connect. Set false to disable.
constexpr bool ENABLE_AP_FALLBACK = true;
constexpr char AP_SSID[] = "BoomCtrl";
