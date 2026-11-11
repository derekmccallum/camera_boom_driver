// 03_wifi_ap_test
// Joins your home WiFi (DHCP address) and serves a test page with an LED toggle.
// If the home network can't be joined, optionally falls back to its own access
// point. No extra libraries: WiFi, WebServer and ESPmDNS ship with the ESP32 core.
//
// STATUS: the earlier access-point-only version was run on the real board and
// worked from a tablet. THIS station-mode version is UNTESTED and not yet
// compiled. Flag anything that fails.
//
// SETUP: copy secrets.example.h to secrets.h and fill in your network details.
//   cp secrets.example.h secrets.h
// The ESP32 supports 2.4 GHz WiFi only (no 5 GHz).
//
// The serial monitor prints the address it was given. Open that IP, or
// http://boom.local (mDNS works on macOS/iOS; often unreliable on Android).

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "config.h"
#include "secrets.h"

WebServer server(80);
bool ledOn = false;

const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Boom Control Test</title>
<style>
  body { font-family: sans-serif; text-align: center; margin: 2em; }
  button { font-size: 1.5em; padding: 0.8em 1.5em; margin: 0.4em; }
</style></head><body>
<h1>ESP32 is up</h1>
<p>Uptime: <span id="up">-</span> s | LED: <span id="led">-</span></p>
<button onclick="send('/led/on')">LED ON</button>
<button onclick="send('/led/off')">LED OFF</button>
<script>
function send(path) { fetch(path).then(update); }
function update() {
  fetch('/status').then(r => r.json()).then(s => {
    document.getElementById('up').textContent = s.uptime;
    document.getElementById('led').textContent = s.led ? 'ON' : 'OFF';
  });
}
setInterval(update, 1000);
update();
</script></body></html>
)rawliteral";

void handleRoot()  { server.send_P(200, "text/html", PAGE); }
void handleOn()    { ledOn = true;  digitalWrite(PIN_LED, HIGH); server.send(200, "text/plain", "ok"); }
void handleOff()   { ledOn = false; digitalWrite(PIN_LED, LOW);  server.send(200, "text/plain", "ok"); }
void handleStatus() {
  String json = "{\"uptime\":" + String(millis() / 1000) +
                ",\"led\":" + (ledOn ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

// Join the home network and wait for a DHCP lease. Returns true on success.
bool startStation() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Connecting to '%s'", WIFI_SSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("Could not join '%s' (status %d).\n", WIFI_SSID, (int)WiFi.status());
    return false;
  }
  Serial.print("Connected. DHCP address: http://");
  Serial.println(WiFi.localIP());
  Serial.printf("Signal: %d dBm\n", WiFi.RSSI());
  return true;
}

void startAccessPoint() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.printf("Fallback AP started. Join '%s' and browse to http://", AP_SSID);
  Serial.println(WiFi.softAPIP());   // normally 192.168.4.1
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(PIN_LED, OUTPUT);

  if (!startStation()) {
    if (ENABLE_AP_FALLBACK) {
      startAccessPoint();
    } else {
      Serial.println("No network and AP fallback disabled. The web page is unreachable.");
    }
  }

  if (MDNS.begin(HOSTNAME)) {
    Serial.printf("Also try http://%s.local\n", HOSTNAME);
  }

  server.on("/", handleRoot);
  server.on("/led/on", handleOn);
  server.on("/led/off", handleOff);
  server.on("/status", handleStatus);
  server.begin();
}

void loop() {
  server.handleClient();
}
