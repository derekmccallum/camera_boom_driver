// config.h - pins and tunables for the web-controlled limit shuttle.
// Tested on the boom (sketch 07 passed). R_SENSE below is 0.05 ohm (Adafruit TMC2209 schematic); changed from 0.11 on 2026-11-20 with the current raised to 740 mA, so the real current on the Adafruit board is about 740 mA, as when sketch 08 passed.
// Sensor polarity was measured on the bench: both active-HIGH (see below).
// Network name and passwords live in secrets.h (gitignored).
#pragma once

// ---- Motor driver pins (same as sketches 04 to 06) ----
constexpr int PIN_STEP = 25;
constexpr int PIN_DIR  = 26;
constexpr int PIN_EN   = 27;   // active LOW (HIGH = motor disabled)

// ---- TMC2208 single-wire UART (Serial2) ----
constexpr int PIN_UART_RX = 16;
constexpr int PIN_UART_TX = 17;
constexpr unsigned long UART_BAUD = 115200;

// Adafruit TMC2209 breakout sense resistors R1/R2 = 0.05 ohm (Adafruit's Eagle schematic, checked
// 2026-11-20). Was 0.11 (assumed for the BTT TMC2208 V3.0).
constexpr float R_SENSE = 0.05f;

// ---- Motor / driver settings ----
constexpr uint16_t MOTOR_RMS_CURRENT_MA = 740;   // low on purpose; unloaded motor test; was 400 at R_SENSE 0.11; 740 here is the same real current
constexpr uint16_t MICROSTEPS           = 16;
constexpr uint16_t MOTOR_FULL_STEPS_REV = 200;   // 1.8 degree motor
constexpr int32_t  STEPS_PER_REV = (int32_t)MOTOR_FULL_STEPS_REV * MICROSTEPS;  // 3200

// ---- Limit sensors (BIQU optical endstops, 3.3 V, input-only pins, no pull-up) ----
// CW limit = the end reached by turning clockwise, viewed from above.
constexpr int PIN_LIMIT_CW  = 34;
constexpr int PIN_LIMIT_CCW = 35;

// Bench-measured with sketch 06's '?' command: with this wiring BOTH sensors read
// HIGH when the beam is blocked and LOW when it is clear (active-HIGH), and the
// sensor LED is lit when blocked. Check with '?' (raw levels) if a sensor is ever
// swapped or rewired.
constexpr bool CW_BLOCKED_IS_HIGH  = true;
constexpr bool CCW_BLOCKED_IS_HIGH = true;

// ---- Direction ----
// Clockwise is viewed from above, looking down at the boom's pivot. Which DIR
// level turns the boom clockwise depends on how the motor is mounted: watch the
// first run, and flip this if the boom goes the wrong way.
// (The wrong way is caught as "wrong limit" when it reaches the other sensor.)
constexpr bool CW_IS_DIR_HIGH = true;

// ---- Motion (units: microsteps/s and microsteps/s^2) ----
constexpr uint32_t SEEK_SPEED_HZ   = 400;    // slow: seek CW limit and measure travel (not adjustable)

// Shuttle leg speed, adjustable live from the web slider. The motor is stopped
// abruptly (forceStop) at each sensor, so the overshoot past the sensor grows with
// this speed. Keep the maximum modest until the real mechanics and sensor
// positions are known. The server clamps any request into this range.
constexpr uint32_t LEG_SPEED_MIN_HZ     = 100;
constexpr uint32_t LEG_SPEED_MAX_HZ     = 1600;
constexpr uint32_t LEG_SPEED_DEFAULT_HZ = 400;

constexpr uint32_t ACCEL_STEPS_S2  = 1600;   // used on every move (also the web STOP deceleration)
constexpr unsigned long PAUSE_MS   = 1000;   // pause at each limit before reversing

// ---- Sensor debounce ----
// A sensor changes state only after DEBOUNCE_COUNT consecutive samples that
// agree, taken at least SENSOR_SAMPLE_US apart. 3 x 1000 us = at least 2 ms
// between the first and the last sample. (Sensors are read on every loop();
// only reads that are SENSOR_SAMPLE_US apart count towards the streak.)
constexpr uint8_t  DEBOUNCE_COUNT   = 3;
constexpr unsigned long SENSOR_SAMPLE_US = 1000;

// ---- Safety limits ----
// Far sensor must be reached within this many steps of the start of a seek or
// measure move. Finite cap for bench use: reduce it once the real travel is known.
constexpr int32_t MAX_SEEK_STEPS = 64000;      // 20 revs of the bare motor
// Measured travel below this is a fault (a sensor is probably stuck or mis-wired).
constexpr int32_t MIN_TRAVEL_STEPS = 800;
// A shuttle leg is a move to the far limit's expected position plus this many
// steps. If the sensor has not tripped by then it is a fault. Covers position
// error from lost steps; it also bounds how far a missed sensor can run.
constexpr int32_t LEG_OVERRUN_STEPS = 6400;
// When a move starts with the sensor we are leaving still blocked, it must clear
// within this many steps. Otherwise the motor is probably going the wrong way.
constexpr int32_t LEAVE_MAX_STEPS = 2 * STEPS_PER_REV;

// If the position at a limit differs from the expected one by more than this,
// the position is re-zeroed to the expected value. Smaller errors are only logged.
constexpr int32_t ERROR_REZERO_STEPS = 50;

// A user STOP must bring the motor to rest within this time (the ramp from
// LEG_SPEED_MAX_HZ at ACCEL_STEPS_S2 takes about 1 s), or it is a fault.
constexpr unsigned long HALT_TIMEOUT_MS = 3000;

// ---- WiFi / web ----
// Station mode: join the home network and take a DHCP address.
constexpr char HOSTNAME[] = "boom";   // DHCP hostname; also http://boom.local via mDNS
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

// If the home network is not found (e.g. on location), start our own access
// point instead so the tablet can still connect. Set false to disable.
constexpr bool ENABLE_AP_FALLBACK = true;
constexpr char AP_SSID[] = "BoomCtrl";
