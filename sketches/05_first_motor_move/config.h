// config.h - pins and tunables for the first motor move test.
// UNTESTED on hardware. R_SENSE below is still NOT verified.
#pragma once

// Motor driver pins
constexpr int PIN_STEP = 25;
constexpr int PIN_DIR  = 26;
constexpr int PIN_EN   = 27;   // active LOW (HIGH = motor disabled)

// TMC2208 single-wire UART (Serial2)
constexpr int PIN_UART_RX = 16;
constexpr int PIN_UART_TX = 17;
constexpr unsigned long UART_BAUD = 115200;

// NOT VERIFIED: assumed sense resistor for BIGTREETECH TMC2208 V3.0
constexpr float R_SENSE = 0.11f;

// Motor / driver settings
constexpr uint16_t MOTOR_RMS_CURRENT_MA = 400;   // low on purpose; unloaded motor
constexpr uint16_t MICROSTEPS           = 16;
constexpr uint16_t MOTOR_FULL_STEPS_REV = 200;   // 1.8 degree motor (assumed)
constexpr int32_t  STEPS_PER_REV = (int32_t)MOTOR_FULL_STEPS_REV * MICROSTEPS;  // 3200

// Motion (units: microsteps/s and microsteps/s^2)
constexpr uint32_t START_SPEED_HZ = 800;    // 0.25 rev/s
constexpr uint32_t MIN_SPEED_HZ   = 200;
constexpr uint32_t MAX_SPEED_HZ   = 3200;   // 1 rev/s hard cap for this test
constexpr uint32_t SPEED_STEP_HZ  = 200;
constexpr uint32_t ACCEL          = 1600;

// Safety: driver is disabled again after this long with no motion
constexpr unsigned long IDLE_DISABLE_MS = 15000;
// Pause between legs of the automatic test
constexpr unsigned long TEST_PAUSE_MS = 1000;
