// 05_first_motor_move
// First motor motion: unloaded NEMA 17, low current, slow, short finite moves.
//
// SETUP (power OFF before connecting or moving the motor wires):
//   - Motor UNLOADED: no pulley, belt or boom. Put a piece of tape on the shaft
//     as a mark so you can count turns.
//   - Identify the two coil pairs with a multimeter (low resistance, a few ohms,
//     between the two wires of a pair; open circuit between pairs). Connect one
//     pair to OA1/OA2 and the other to OB1/OB2 (this board's labels). Never connect or disconnect the
//     motor while the 12 V supply is on: it can destroy the driver.
//   - 12 V on VM/GND with the 100 uF cap, ESP32 on USB, grounds common.
//   - Keep a hand on the e-stop / power switch for the first moves.
//   - Wrong pairing or a swapped wire makes the motor buzz or shudder instead
//     of turning. Power off and fix the wiring. A reversed single coil just
//     reverses direction, which is harmless.
//
// The driver starts DISABLED. Nothing moves until you send 'e', then a move.
// It disables itself again after IDLE_DISABLE_MS with no motion.
//
// Serial monitor: 115200 baud, line ending can be anything.
//
// STATUS: UNTESTED on hardware. Written without hardware access; report any
// compile errors or odd behaviour.
//
// Libraries: TMCStepper, FastAccelStepper (Arduino Library Manager).

#include <TMCStepper.h>
#include <FastAccelStepper.h>
#include "config.h"

TMC2208Stepper driver(&Serial2, R_SENSE);
FastAccelStepperEngine engine;
FastAccelStepper *stepper = nullptr;

uint32_t speedHz = START_SPEED_HZ;
bool enabled = false;
unsigned long lastActiveMs = 0;

// Automatic test: forward 1 rev, pause, back 1 rev. Finishes at position 0.
enum TestState { T_IDLE, T_FWD, T_PAUSE, T_BACK };
TestState testState = T_IDLE;
unsigned long testPauseStart = 0;

static void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  e  enable motor (holds position, shaft will feel stiff)");
  Serial.println("  d  disable motor");
  Serial.println("  f  forward 1 rev        r  reverse 1 rev");
  Serial.println("  F  forward 1/8 rev      R  reverse 1/8 rev");
  Serial.println("  t  auto test: forward 1 rev, pause, back 1 rev");
  Serial.println("  s  STOP (decelerates, motor stays enabled)");
  Serial.println("  +  / -  speed up / down");
  Serial.println("  z  zero the position counter");
  Serial.println("  ?  status and driver diagnostics");
}

static void printStatus() {
  Serial.println();
  Serial.printf("enabled: %s  speed: %lu Hz (%.2f rev/s)  position: %ld steps (%.3f rev)\n",
                enabled ? "YES" : "no", (unsigned long)speedHz,
                (float)speedHz / STEPS_PER_REV,
                (long)stepper->getCurrentPosition(),
                (float)stepper->getCurrentPosition() / STEPS_PER_REV);
  Serial.printf("moving: %s  test: %s\n", stepper->isRunning() ? "yes" : "no",
                testState == T_IDLE ? "idle" : "running");

  uint32_t drv = driver.DRV_STATUS();
  if (drv == 0 || drv == 0xFFFFFFFF) {
    Serial.println("driver: no valid UART reply (check wiring / 12 V)");
    return;
  }
  Serial.printf("driver: rms_current %u mA, microsteps %u, cs_actual %u\n",
                driver.rms_current(), driver.microsteps(), driver.cs_actual());
  Serial.printf("  over-temp: %u  pre-warn: %u\n", driver.ot(), driver.otpw());
  Serial.printf("  short to GND  A:%u B:%u   short to supply  A:%u B:%u\n",
                driver.s2ga(), driver.s2gb(), driver.s2vsa(), driver.s2vsb());
  Serial.printf("  open load     A:%u B:%u (only meaningful while stationary/enabled; 1 = coil not connected)\n",
                driver.ola(), driver.olb());
  Serial.printf("  GSTAT: reset %u  drv_err %u  uv_cp %u\n",
                driver.reset(), driver.drv_err(), driver.uv_cp());
}

static bool configureDriver() {
  Serial2.begin(UART_BAUD, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
  driver.begin();
  delay(100);

  uint32_t ioin = driver.IOIN();
  uint8_t version = (ioin >> 24) & 0xFF;
  if (ioin == 0 || ioin == 0xFFFFFFFF) {
    Serial.println("ERROR: no UART reply from TMC2208. Check 12 V and wiring.");
    return false;
  }
  Serial.printf("TMC2208 replied, chip version 0x%02X\n", version);

  driver.pdn_disable(true);        // use UART, not the PDN pin
  driver.I_scale_analog(false);    // current from UART, not VREF pot
  driver.mstep_reg_select(true);   // microsteps from UART, not MS1/MS2
  driver.toff(5);
  driver.microsteps(MICROSTEPS);
  driver.rms_current(MOTOR_RMS_CURRENT_MA);
  driver.en_spreadCycle(false);    // StealthChop: quiet
  driver.pwm_autoscale(true);

  bool ok = driver.microsteps() == MICROSTEPS;
  Serial.printf("Configured: %u mA, %u microsteps -> %s\n",
                driver.rms_current(), driver.microsteps(), ok ? "OK" : "READBACK MISMATCH");
  return ok;
}

static void enableMotor() {
  stepper->enableOutputs();
  enabled = true;
  lastActiveMs = millis();
  Serial.println("Motor ENABLED");
}

static void disableMotor() {
  testState = T_IDLE;
  stepper->forceStop();
  stepper->disableOutputs();
  enabled = false;
  Serial.println("Motor DISABLED");
}

static void startMove(int32_t steps) {
  if (!enabled) {
    Serial.println("Not enabled. Send 'e' first.");
    return;
  }
  if (stepper->isRunning()) {
    Serial.println("Already moving. Send 's' to stop first.");
    return;
  }
  stepper->setSpeedInHz(speedHz);
  stepper->setAcceleration(ACCEL);
  stepper->move(steps);
  lastActiveMs = millis();
  Serial.printf("Move %+ld steps at %lu Hz\n", (long)steps, (unsigned long)speedHz);
}

static void startTest() {
  if (!enabled) {
    Serial.println("Not enabled. Send 'e' first.");
    return;
  }
  if (stepper->isRunning()) {
    Serial.println("Already moving. Send 's' to stop first.");
    return;
  }
  Serial.println("Test: forward 1 rev, pause, back 1 rev");
  testState = T_FWD;
  startMove(STEPS_PER_REV);
}

static void stopMotion() {
  testState = T_IDLE;
  stepper->stopMove();   // decelerate to a stop
  Serial.println("STOP");
}

static void updateTest() {
  if (testState == T_IDLE) return;
  switch (testState) {
    case T_FWD:
      if (!stepper->isRunning()) {
        testState = T_PAUSE;
        testPauseStart = millis();
      }
      break;
    case T_PAUSE:
      if (millis() - testPauseStart >= TEST_PAUSE_MS) {
        testState = T_BACK;
        startMove(-STEPS_PER_REV);
      }
      break;
    case T_BACK:
      if (!stepper->isRunning()) {
        testState = T_IDLE;
        Serial.printf("Test done. Position %ld steps (expect 0). Does the tape mark point where it started?\n",
                      (long)stepper->getCurrentPosition());
      }
      break;
    default:
      break;
  }
}

static void handleChar(char c) {
  switch (c) {
    case 'e': enableMotor(); break;
    case 'd': disableMotor(); break;
    case 'f': startMove(STEPS_PER_REV); break;
    case 'r': startMove(-STEPS_PER_REV); break;
    case 'F': startMove(STEPS_PER_REV / 8); break;
    case 'R': startMove(-STEPS_PER_REV / 8); break;
    case 't': startTest(); break;
    case 's': stopMotion(); break;
    case '+':
      speedHz = min(speedHz + SPEED_STEP_HZ, MAX_SPEED_HZ);
      Serial.printf("Speed %lu Hz (applies to next move)\n", (unsigned long)speedHz);
      break;
    case '-':
      speedHz = (speedHz > MIN_SPEED_HZ + SPEED_STEP_HZ) ? speedHz - SPEED_STEP_HZ : MIN_SPEED_HZ;
      Serial.printf("Speed %lu Hz (applies to next move)\n", (unsigned long)speedHz);
      break;
    case 'z':
      if (stepper->isRunning()) {
        Serial.println("Stop first.");
      } else {
        stepper->setCurrentPosition(0);
        Serial.println("Position zeroed");
      }
      break;
    case '?': printStatus(); break;
    case '\r':
    case '\n':
    case ' ':
      break;
    default:
      Serial.printf("Unknown command '%c'\n", c);
      printHelp();
      break;
  }
}

void setup() {
  // Keep the driver disabled from the very first moment.
  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, HIGH);
  pinMode(PIN_STEP, OUTPUT);
  digitalWrite(PIN_STEP, LOW);
  pinMode(PIN_DIR, OUTPUT);
  digitalWrite(PIN_DIR, LOW);

  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== 05 first motor move (UNLOADED motor only) ===");

  bool driverOk = configureDriver();

  engine.init();
  stepper = engine.stepperConnectToPin(PIN_STEP);
  if (!stepper) {
    Serial.println("ERROR: could not attach stepper to STEP pin. Halting.");
    while (true) delay(1000);
  }
  stepper->setDirectionPin(PIN_DIR);
  stepper->setEnablePin(PIN_EN);      // active low by default
  stepper->setAutoEnable(false);      // we enable/disable explicitly
  stepper->disableOutputs();
  stepper->setCurrentPosition(0);
  stepper->setSpeedInHz(speedHz);
  stepper->setAcceleration(ACCEL);

  if (!driverOk) {
    Serial.println("Driver config problem: moves are NOT recommended until fixed.");
  }
  Serial.printf("Motor %u mA, %u microsteps, %ld steps/rev, speed %lu Hz, accel %lu\n",
                MOTOR_RMS_CURRENT_MA, MICROSTEPS, (long)STEPS_PER_REV,
                (unsigned long)speedHz, (unsigned long)ACCEL);
  Serial.println("Motor is DISABLED. Send 'e' to enable.");
  printHelp();
}

void loop() {
  while (Serial.available()) handleChar((char)Serial.read());

  updateTest();

  if (stepper->isRunning()) {
    lastActiveMs = millis();
  } else if (enabled && millis() - lastActiveMs >= IDLE_DISABLE_MS) {
    Serial.println("Idle timeout.");
    disableMotor();
  }
}
