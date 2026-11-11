// config.h - pins and tunables for the optical endstop test.
#pragma once

constexpr int PIN_SENSOR = 34;   // input-only pin, no internal pull-up (the sensor board has its own)
constexpr int PIN_LED    = 2;    // onboard LED; use an external LED on another pin if preferred

// This BIQU optical endstop was TESTED as active-LOW: the signal goes LOW when
// the beam is blocked. Set to true if a different sensor behaves the other way.
constexpr bool BLOCKED_IS_HIGH = false;

constexpr unsigned long DEBOUNCE_MS = 10;   // raise to 20-30 if transitions print more than once
