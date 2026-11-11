// 04_tmc2208_uart_test
// Checks single-wire UART communication with a TMC2208. No motion.
//
// SETUP: motor DISCONNECTED, 12 V on VM/GND only, ESP32 powered by USB,
// grounds common. EN is held HIGH (driver disabled) the whole time.
//
// STATUS: tested on hardware, RESULT: PASS (chip version 0x20, writes counted,
// read-back OK). PDN_UART is the 4th header pin counting EN as 1; both the RX
// wire and the TX resistor must be on that same pin.
//
// Library: TMCStepper (Arduino Library Manager).

#include <TMCStepper.h>
#include "config.h"

TMC2208Stepper driver(&Serial2, R_SENSE);

static void printHex(const char *label, uint32_t v) {
  Serial.printf("  %-10s 0x%08lX\n", label, (unsigned long)v);
}

static void runTest() {
  Serial.println();
  Serial.println("=== TMC2208 UART test ===");

  // 1. Raw reads: all zeros or all ones means no valid reply.
  uint32_t ioin = driver.IOIN();
  uint32_t gconf = driver.GCONF();
  uint32_t drv = driver.DRV_STATUS();
  Serial.println("Raw registers (before config):");
  printHex("IOIN", ioin);
  printHex("GCONF", gconf);
  printHex("DRV_STATUS", drv);

  // IOIN bits 31..24 are the chip version; a TMC2208 reports 0x20 or 0x21.
  uint8_t version = (ioin >> 24) & 0xFF;
  Serial.printf("  chip version byte: 0x%02X (expect 0x20/0x21)\n", version);

  uint8_t tc = driver.test_connection();
  Serial.printf("test_connection(): %u (0 = OK)\n", tc);

  // 2. Write test: IFCNT increments on every successful write.
  uint8_t before = driver.IFCNT();
  driver.pdn_disable(true);        // use UART, not the PDN pin
  driver.I_scale_analog(false);    // current from UART, not VREF pot
  driver.mstep_reg_select(true);   // microsteps from UART, not MS1/MS2
  driver.toff(5);
  driver.microsteps(TEST_MICROSTEPS);
  driver.rms_current(TEST_RMS_CURRENT_MA);
  uint8_t after = driver.IFCNT();
  Serial.printf("IFCNT before=%u after=%u (writes counted: %u)\n",
                before, after, (uint8_t)(after - before));

  // 3. Read back what we wrote.
  Serial.println("Readback:");
  Serial.printf("  microsteps: %u (wrote %u)\n", driver.microsteps(), TEST_MICROSTEPS);
  Serial.printf("  rms_current: %u mA (wrote %u)\n", driver.rms_current(), TEST_RMS_CURRENT_MA);
  Serial.printf("  toff: %u (wrote 5)\n", driver.toff());

  // 4. Verdict
  bool replied = (ioin != 0 && ioin != 0xFFFFFFFF);
  bool wrote = (uint8_t)(after - before) > 0;
  bool readback = driver.microsteps() == TEST_MICROSTEPS;
  Serial.println();
  if (replied && wrote && readback) {
    Serial.println("RESULT: PASS - UART read and write both work.");
  } else if (!replied) {
    Serial.println("RESULT: FAIL - no valid reply. Check: VM 12 V present, VIO 3.3 V,");
    Serial.println("  J2 fitted (UART mode), 1k on TX, RX direct, GND common, pins 16/17.");
  } else {
    Serial.println("RESULT: PARTIAL - driver replies but writes did not stick.");
    Serial.println("  Check the 1k TX resistor and wiring.");
  }
  Serial.println("Send any character to run again.");
}

void setup() {
  // Disable the driver before anything else.
  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, HIGH);
  pinMode(PIN_STEP, OUTPUT);
  digitalWrite(PIN_STEP, LOW);
  pinMode(PIN_DIR, OUTPUT);
  digitalWrite(PIN_DIR, LOW);

  Serial.begin(115200);
  delay(500);

  Serial2.begin(UART_BAUD, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
  driver.begin();
  delay(100);

  runTest();
}

void loop() {
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    runTest();
  }
}
