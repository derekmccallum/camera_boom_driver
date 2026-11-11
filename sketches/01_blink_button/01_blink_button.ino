// 01_blink_button
// Proves the toolchain, upload, serial and input reading work.
//
// STATUS: an earlier version of this sketch was run on the real board and
// worked (LED toggled, serial output correct). It has since been split out
// into config.h and has NOT been recompiled. Flag anything that fails.
//
// Wiring (optional): button between GPIO 32 and GND.

#include "config.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  Serial.println("ESP32 up. Blink + button test.");
}

void loop() {
  static bool led = false;
  static unsigned long last = 0;

  if (millis() - last >= 500) {
    last = millis();
    led = !led;
    digitalWrite(PIN_LED, led);
    Serial.printf("LED %s, button %s\n",
                  led ? "ON" : "OFF",
                  digitalRead(PIN_BTN) == LOW ? "PRESSED" : "released");
  }
}
