// config.h - pins and tunables for the TMC2208 UART test.
// UNTESTED: written without hardware access.
#pragma once

// Motor driver pins
constexpr int PIN_STEP = 25;
constexpr int PIN_DIR  = 26;
constexpr int PIN_EN   = 27;   // active LOW (HIGH = motor disabled)

// TMC2208 single-wire UART (Serial2)
//   ESP32 RX (GPIO16) -> directly to PDN_UART
//   ESP32 TX (GPIO17) -> 1k resistor -> PDN_UART
constexpr int PIN_UART_RX = 16;
constexpr int PIN_UART_TX = 17;
constexpr unsigned long UART_BAUD = 115200;

// NOT VERIFIED: assumed sense resistor for BIGTREETECH TMC2208 V3.0
constexpr float R_SENSE = 0.11f;

// Configuration written during the test (EN stays HIGH, so no current flows)
constexpr uint16_t TEST_RMS_CURRENT_MA = 400;
constexpr uint16_t TEST_MICROSTEPS     = 16;
