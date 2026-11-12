// config.h - pins and tunables for the optical endstop test.
#pragma once

constexpr int PIN_SENSOR = 34;   // input-only pin, no internal pull-up (the sensor board has its own)
constexpr int PIN_LED    = 2;    // onboard LED; use an external LED on another pin if preferred

// As currently wired, this BIQU optical endstop is ACTIVE-HIGH: the pin reads LOW
// with the beam clear and HIGH when blocked (measured with sketch 06's '?' command).
// Set to false if a different sensor or wiring behaves the other way.
// NOTE: this value has not been re-run on hardware since being changed from false.
constexpr bool BLOCKED_IS_HIGH = true;

constexpr unsigned long DEBOUNCE_MS = 10;   // raise to 20-30 if transitions print more than once
