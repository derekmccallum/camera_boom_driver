// 02_opto_endstop
// Tests a 3-pin optical endstop (BIQU / TCST2103 type).
// LED solid when the beam is clear, flashing when blocked.
//
// STATUS: an earlier version was run on the real sensor and worked once the
// polarity was set to active-LOW. Since split into config.h; NOT recompiled.
//
// Wiring (use the labels printed on the sensor PCB, not the cable plug order):
//   sensor V  -> ESP32 3V3        (NOT 5V: the signal could then reach 5V)
//   sensor G  -> ESP32 GND
//   sensor S  -> GPIO 34
// With the sensor unplugged, GPIO 34 floats and can read anything.
//
// Note: a mechanical normally-closed switch fails safe on a broken wire; an
// optical sensor that loses power just reads "clear". Keep a physical stop.

#include "config.h"

bool blocked = false;       // debounced state
bool lastRaw = false;
unsigned long lastChange = 0;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SENSOR, INPUT);
  pinMode(PIN_LED, OUTPUT);
  Serial.println("Opto limit switch test");
}

void loop() {
  bool raw = (digitalRead(PIN_SENSOR) == HIGH) == BLOCKED_IS_HIGH;

  if (raw != lastRaw) {
    lastRaw = raw;
    lastChange = millis();
  }
  if (millis() - lastChange >= DEBOUNCE_MS && raw != blocked) {
    blocked = raw;
    Serial.println(blocked ? "BLOCKED (closed)" : "CLEAR (open)");
  }

  if (blocked) {
    digitalWrite(PIN_LED, (millis() / 125) % 2);   // flash at about 4 Hz
  } else {
    digitalWrite(PIN_LED, HIGH);                    // solid
  }
}
