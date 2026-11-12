// config.h - pins and tunables for the limit-to-limit shuttle test.
// UNTESTED on hardware as a full run. R_SENSE below is still NOT verified.
// Sensor polarity was measured on the bench: both active-HIGH (see below).
#pragma once

// ---- Motor driver pins (same as sketches 04 and 05) ----
constexpr int PIN_STEP = 25;
constexpr int PIN_DIR  = 26;
constexpr int PIN_EN   = 27;   // active LOW (HIGH = motor disabled)

// ---- TMC2208 single-wire UART (Serial2) ----
constexpr int PIN_UART_RX = 16;
constexpr int PIN_UART_TX = 17;
constexpr unsigned long UART_BAUD = 115200;

// NOT VERIFIED: assumed sense resistor for BIGTREETECH TMC2208 V3.0
constexpr float R_SENSE = 0.11f;

// ---- Motor / driver settings ----
constexpr uint16_t MOTOR_RMS_CURRENT_MA = 400;   // low on purpose; unloaded motor test
constexpr uint16_t MICROSTEPS           = 16;
constexpr uint16_t MOTOR_FULL_STEPS_REV = 200;   // 1.8 degree motor
constexpr int32_t  STEPS_PER_REV = (int32_t)MOTOR_FULL_STEPS_REV * MICROSTEPS;  // 3200

// ---- Limit sensors (BIQU optical endstops, 3.3 V, input-only pins, no pull-up) ----
// CW limit = the end reached by turning clockwise, viewed from above.
constexpr int PIN_LIMIT_CW  = 34;
constexpr int PIN_LIMIT_CCW = 35;

// Bench-measured with sketch 06's '?' command: with this wiring BOTH sensors read
// HIGH when the beam is blocked and LOW when it is clear (active-HIGH), and the
// sensor LED is lit when blocked. Confirmed correct by the user; older notes that
// said GPIO 34 was active-LOW have been corrected.
// Check with '?' (raw levels) if a sensor is ever swapped or rewired.
constexpr bool CW_BLOCKED_IS_HIGH  = true;
constexpr bool CCW_BLOCKED_IS_HIGH = true;

// ---- Direction ----
// Clockwise is viewed from above, looking down at the boom's pivot. Which DIR
// level turns the boom clockwise depends on how the motor is mounted: watch the
// first run, and flip this if the boom goes the wrong way.
// (The wrong way is caught as "wrong limit" when it reaches the other sensor.)
constexpr bool CW_IS_DIR_HIGH = true;

// ---- Motion (units: microsteps/s and microsteps/s^2) ----
constexpr uint32_t SEEK_SPEED_HZ   = 400;    // slow: seek CW limit and measure travel
// Constant speed for every shuttle leg, accelerated up to and run until the far
// sensor trips. The motor is stopped abruptly (forceStop) at the sensor, so the
// overshoot past the sensor grows with this speed: raise it in small steps.
constexpr uint32_t LEG_SPEED_HZ    = 400;
constexpr uint32_t ACCEL_STEPS_S2  = 1600;   // used on every move
constexpr unsigned long PAUSE_MS   = 1000;   // pause at each limit before reversing
constexpr unsigned long COUNTDOWN_S = 5;     // safety countdown before the motor is enabled

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
