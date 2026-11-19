// Copy this file to secrets.h (which is gitignored) and fill in real values.
// NEVER commit secrets.h.
#pragma once

#define WIFI_SSID     "your-home-network-name"   // must be a 2.4 GHz network
#define WIFI_PASSWORD "your-home-wifi-password"

// Fallback access point password (used only if the home network can't be joined).
// WPA2 needs 8 or more characters.
#define AP_PASSWORD   "change-me-1234"
