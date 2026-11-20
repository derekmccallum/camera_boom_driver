// config.h - pins and tunables for the TMC2208 UART test.
// Tested on hardware: UART test passes. R_SENSE below is 0.05 ohm (Adafruit TMC2209 schematic); changed from 0.11 on 2026-11-20 with the current raised to 740 mA, so the real current on the Adafruit board is about 740 mA, as when sketch 08 passed.
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

// Adafruit TMC2209 breakout sense resistors R1/R2 = 0.05 ohm (Adafruit's Eagle schematic, checked
// 2026-11-20). Was 0.11 (assumed for the BTT TMC2208 V3.0).
constexpr float R_SENSE = 0.05f;

// Configuration written during the test (EN stays HIGH, so no current flows)
constexpr uint16_t TEST_RMS_CURRENT_MA = 740;  // was 400 at R_SENSE 0.11; 740 here is the same real current
constexpr uint16_t TEST_MICROSTEPS     = 16;
