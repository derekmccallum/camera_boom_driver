// config.h - pins and tunables for the first motor move test.
// Tested on hardware: passed. R_SENSE below is 0.05 ohm (Adafruit TMC2209 schematic); changed from 0.11 on 2026-11-20 with the current raised to 740 mA, so the real current on the Adafruit board is about 740 mA, as when sketch 08 passed.
#pragma once

// Motor driver pins
constexpr int PIN_STEP = 25;
constexpr int PIN_DIR  = 26;
constexpr int PIN_EN   = 27;   // active LOW (HIGH = motor disabled)

// TMC2208 single-wire UART (Serial2)
constexpr int PIN_UART_RX = 16;
constexpr int PIN_UART_TX = 17;
constexpr unsigned long UART_BAUD = 115200;

// Adafruit TMC2209 breakout sense resistors R1/R2 = 0.05 ohm (Adafruit's Eagle schematic, checked
// 2026-11-20). Was 0.11 (assumed for the BTT TMC2208 V3.0).
constexpr float R_SENSE = 0.05f;

// Motor / driver settings
constexpr uint16_t MOTOR_RMS_CURRENT_MA = 740;   // low on purpose; unloaded motor; was 400 at R_SENSE 0.11; 740 here is the same real current
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
