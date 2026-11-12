// 06_limit_shuttle
// Unloaded limit-to-limit shuttle. After a safety countdown the motor enables
// itself, turns CLOCKWISE (viewed from above) to the CW sensor, treats that as
// position 0, measures the travel to the CCW sensor, then shuttles between the
// two sensors forever: run at one constant speed until the far sensor trips, pause,
// reverse. The position error at each hit is logged to show missed steps.
//
// THIS AUTO-STARTS after the countdown. Keep a hand on the e-stop / power
// switch, keep the boom area clear, and keep a physical stop in place.
// Send 's' in the serial monitor to abort at any time.
//
// STATUS: UNTESTED on hardware. Written without hardware access; report any
// compile errors or odd behaviour. The FastAccelStepper calls were checked
// against the installed library header (version 2.0.1), not run.
//
// WIRING (use the labels printed on each sensor PCB, not the cable plug order):
//   CW  sensor:  V -> ESP32 3V3 (NOT 5V),  G -> ESP32 GND,  S -> GPIO 34
//   CCW sensor:  V -> ESP32 3V3 (NOT 5V),  G -> ESP32 GND,  S -> GPIO 35
//   Driver as in sketch 05: STEP 25, DIR 26, EN 27 (active LOW),
//   UART RX 16 direct to PDN_UART, TX 17 via 1k to PDN_UART.
//   12 V on the driver BEFORE the ESP32 boots (the driver is configured once at boot).
//   An optical sensor that loses power just reads "clear": the soft checks here
//   do not replace a physical stop.
//
// POSITION CONVENTION: 0 = CW limit. Position counts UP towards the CCW limit.
// The CCW limit is at travelSteps. Position 0 is where the motor actually
// stopped after the CW sensor tripped (a few steps past the trigger point).
//
// FAULTS (motor disabled, stays stopped until the board is reset):
//   - the sensor for the opposite direction trips, or both are blocked at once
//   - the far sensor is not reached within MAX_SEEK_STEPS (seek, measure) or
//     LEG_OVERRUN_STEPS past the expected limit position (shuttle legs)
//   - the sensor being left does not clear within LEAVE_MAX_STEPS
//   - measured travel is below MIN_TRAVEL_STEPS
//   - the driver read-back at boot does not match (motor never enabled)
//
// Serial monitor: 115200 baud. Commands: s stop+disable, d disable, ? status.
//
// Libraries: TMCStepper, FastAccelStepper (Arduino Library Manager).

#include <TMCStepper.h>
#include <FastAccelStepper.h>
#include "config.h"

TMC2208Stepper driver(&Serial2, R_SENSE);
FastAccelStepperEngine engine;
FastAccelStepper *stepper = nullptr;

enum Dir { DIR_CW, DIR_CCW };

enum State {
  S_DRIVER_FAIL,   // driver read-back failed at boot: motor never enabled
  S_COUNTDOWN,     // motor disabled, waiting to start
  S_SEEK_CW,       // slow, towards the CW sensor
  S_MEASURE,       // slow, CW sensor to CCW sensor
  S_LEG,           // shuttle: run to the far sensor at LEG_SPEED_HZ
  S_STOPPING,      // forceStop issued, waiting for the step queue to drain
  S_PAUSE,         // shuttle: pause at a limit
  S_STOPPED,       // stopped by the user, disabled
  S_FAULT          // fault, disabled
};
static const char *STATE_NAMES[] = {
  "DRIVER_FAIL", "COUNTDOWN", "SEEK_CW", "MEASURE", "LEG",
  "STOPPING", "PAUSE", "STOPPED", "FAULT"
};

// What to do once the motor has really stopped on a limit.
enum AfterStop { AS_NONE, AS_SEEK_CW_DONE, AS_MEASURE_DONE, AS_LIMIT_HIT };

// Debounced optical limit sensor.
struct Limit {
  const char *name;
  int pin;
  bool blockedIsHigh;
  bool blocked;                 // debounced state
  uint8_t streak;               // consecutive differing samples seen so far
  unsigned long lastSampleUs;
};
Limit cwLimit  = { "CW",  PIN_LIMIT_CW,  CW_BLOCKED_IS_HIGH,  false, 0, 0 };
Limit ccwLimit = { "CCW", PIN_LIMIT_CCW, CCW_BLOCKED_IS_HIGH, false, 0, 0 };

State state = S_COUNTDOWN;
AfterStop afterStop = AS_NONE;
Dir moveDir = DIR_CW;           // direction of the current leg / seek
Dir nextLegDir = DIR_CCW;       // direction of the next shuttle leg
int32_t travelSteps = 0;        // measured CW-to-CCW travel
int32_t legStartPos = 0;        // position when this leg began (leave check)
bool otherCleared = false;      // the sensor we are leaving has read clear
unsigned long moveStartMs = 0;
unsigned long stopStartMs = 0;
unsigned long pauseStartMs = 0;
unsigned long countdownStartMs = 0;
unsigned long legCount = 0;

static const char *dirName(Dir d) { return d == DIR_CW ? "CW" : "CCW"; }
static Dir opposite(Dir d) { return d == DIR_CW ? DIR_CCW : DIR_CW; }
static Limit &limitFor(Dir d) { return d == DIR_CW ? cwLimit : ccwLimit; }
static int32_t absI(int32_t v) { return v < 0 ? -v : v; }

// ---- Sensors ----

static bool rawBlocked(const Limit &l) {
  return (digitalRead(l.pin) == HIGH) == l.blockedIsHigh;
}

// Called on every loop(). A change is accepted only after DEBOUNCE_COUNT
// consecutive differing samples, SENSOR_SAMPLE_US apart. Any read that agrees
// with the debounced state restarts the count.
static void updateLimit(Limit &l) {
  bool raw = rawBlocked(l);
  if (raw == l.blocked) {
    l.streak = 0;
    return;
  }
  unsigned long now = micros();
  if (l.streak == 0) {
    l.streak = 1;
    l.lastSampleUs = now;
    return;
  }
  if (now - l.lastSampleUs < SENSOR_SAMPLE_US) return;
  l.lastSampleUs = now;
  if (++l.streak >= DEBOUNCE_COUNT) {
    l.blocked = raw;
    l.streak = 0;
  }
}

static void printSensorLine(const Limit &l) {
  bool high = digitalRead(l.pin) == HIGH;
  Serial.printf("  %-3s (GPIO %d): raw %s -> %s   debounced: %s   (blocked = %s)\n",
                l.name, l.pin, high ? "HIGH" : "LOW",
                (high == l.blockedIsHigh) ? "BLOCKED" : "clear",
                l.blocked ? "BLOCKED" : "clear",
                l.blockedIsHigh ? "HIGH" : "LOW");
}

// ---- Help / status ----

static void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  s  STOP immediately and disable the motor (reset board to restart)");
  Serial.println("  d  disable the motor (same effect: the sequence does not resume)");
  Serial.println("  ?  state, position, sensors and driver diagnostics");
}

static void printStatus() {
  Serial.println();
  Serial.printf("state: %s  position: %ld steps  travelSteps: %ld  legs done: %lu  moving: %s\n",
                STATE_NAMES[state], (long)stepper->getCurrentPosition(),
                (long)travelSteps, legCount, stepper->isRunning() ? "yes" : "no");
  Serial.println("sensors:");
  printSensorLine(cwLimit);
  printSensorLine(ccwLimit);

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

// ---- Driver ----

static bool configureDriver() {
  Serial2.begin(UART_BAUD, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
  driver.begin();
  delay(100);

  uint32_t ioin = driver.IOIN();
  uint8_t version = (ioin >> 24) & 0xFF;
  if (ioin == 0 || ioin == 0xFFFFFFFF) {
    Serial.println("ERROR: no UART reply from TMC2208. Check 12 V (on BEFORE the ESP32 boots) and wiring.");
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

  // Read back: microsteps must match exactly; current is quantised by the
  // driver (sketch 05 read back 397 mA for 400), so allow 10 %.
  int currentErr = absI((int32_t)driver.rms_current() - (int32_t)MOTOR_RMS_CURRENT_MA);
  bool ok = driver.microsteps() == MICROSTEPS &&
            currentErr <= (int)(MOTOR_RMS_CURRENT_MA / 10);
  Serial.printf("Configured: %u mA, %u microsteps -> %s\n",
                driver.rms_current(), driver.microsteps(), ok ? "OK" : "READBACK MISMATCH");
  return ok;
}

// ---- Motor control ----

static void haltAndDisable() {
  stepper->forceStop();
  stepper->disableOutputs();
  digitalWrite(PIN_EN, HIGH);      // belt and braces: EN HIGH = driver off
}

// Fault: stop, disable, print the reason, stay stopped until reset.
static void fault(const char *reason) {
  haltAndDisable();
  state = S_FAULT;
  Serial.println();
  Serial.printf("FAULT: %s\n", reason);
  Serial.printf("Motor DISABLED. Position %ld, travelSteps %ld. Reset the board to restart.\n",
                (long)stepper->getCurrentPosition(), (long)travelSteps);
}

static void userStop(const char *msg) {
  if (state == S_FAULT) {
    Serial.println("Already in FAULT: motor disabled. Reset the board to restart.");
    return;
  }
  haltAndDisable();
  if (state != S_DRIVER_FAIL) state = S_STOPPED;
  Serial.printf("%s: motor DISABLED. Reset the board to restart.\n", msg);
}

// Start an absolute move with the ramp. The ramp parameters are set on every
// move, so every move accelerates and decelerates.
static bool startMoveTo(int32_t target, uint32_t hz) {
  stepper->setSpeedInHz(hz);
  stepper->setAcceleration(ACCEL_STEPS_S2);
  MoveResultCode r = stepper->moveTo(target);
  moveStartMs = millis();
  if (r != MOVE_OK) {
    fault("moveTo() was rejected by FastAccelStepper");
    return false;
  }
  return true;
}

// isRunning() may not have gone true yet in the first loop after moveTo(), so
// a move only counts as finished after a short grace time.
static bool moveFinished() {
  return !stepper->isRunning() && (millis() - moveStartMs > 30);
}

// Record the start of a seek or leg in direction d.
static void beginLeg(Dir d) {
  moveDir = d;
  legStartPos = stepper->getCurrentPosition();
  // The sensor we are leaving may still be blocked; it must clear, and must not
  // trip again once it has.
  otherCleared = !limitFor(opposite(d)).blocked;
}

// Position counts UP towards CCW, so CCW targets are above and CW targets below.
static int32_t seekTarget(Dir d, int32_t cap) {
  int32_t pos = stepper->getCurrentPosition();
  return d == DIR_CCW ? pos + cap : pos - cap;
}

static void startMeasure() {
  beginLeg(DIR_CCW);
  Serial.printf("MEASURE: counter-clockwise at %lu Hz to the CCW sensor (max %ld steps)\n",
                (unsigned long)SEEK_SPEED_HZ, (long)MAX_SEEK_STEPS);
  if (startMoveTo(seekTarget(DIR_CCW, MAX_SEEK_STEPS), SEEK_SPEED_HZ)) state = S_MEASURE;
}

static void startSeekCw() {
  stepper->enableOutputs();
  Serial.println("Motor ENABLED");
  if (cwLimit.blocked) {
    stepper->setCurrentPosition(0);
    Serial.println("SEEK_CW: CW sensor already blocked, treating it as found. Position set to 0.");
    startMeasure();
    return;
  }
  beginLeg(DIR_CW);
  Serial.printf("SEEK_CW: clockwise at %lu Hz to the CW sensor (max %ld steps)\n",
                (unsigned long)SEEK_SPEED_HZ, (long)MAX_SEEK_STEPS);
  if (startMoveTo(seekTarget(DIR_CW, MAX_SEEK_STEPS), SEEK_SPEED_HZ)) state = S_SEEK_CW;
}

// One shuttle leg: a single move at LEG_SPEED_HZ with an acceleration ramp.
// The target is the far limit's expected position plus LEG_OVERRUN_STEPS, so the
// move is finite even if the sensor never trips. The sensor stops it earlier.
static void startLeg(Dir d) {
  beginLeg(d);
  legCount++;
  int32_t target = (d == DIR_CCW) ? travelSteps + LEG_OVERRUN_STEPS : -LEG_OVERRUN_STEPS;
  Serial.printf("LEG %lu: %s at %lu Hz towards the %s sensor (expected at %ld)\n",
                legCount, dirName(d), (unsigned long)LEG_SPEED_HZ, dirName(d),
                (long)((d == DIR_CCW) ? travelSteps : 0));
  if (startMoveTo(target, LEG_SPEED_HZ)) state = S_LEG;
}

// The far sensor tripped: stop abruptly, then handle it once the queue drains.
static void stopAtLimit(AfterStop what) {
  stepper->forceStop();
  afterStop = what;
  stopStartMs = millis();
  state = S_STOPPING;
}

// Motor has really stopped on a limit (position is valid now).
static void handleStopped() {
  int32_t pos = stepper->getCurrentPosition();
  switch (afterStop) {
    case AS_SEEK_CW_DONE:
      stepper->setCurrentPosition(0);
      Serial.printf("CW limit found after %ld steps. Position set to 0.\n", (long)absI(pos));
      startMeasure();
      break;

    case AS_MEASURE_DONE:
      travelSteps = pos;
      Serial.printf("CCW limit found. travelSteps = %ld (%.2f rev of the bare motor)\n",
                    (long)travelSteps, (float)travelSteps / STEPS_PER_REV);
      if (travelSteps < MIN_TRAVEL_STEPS) {
        fault("measured travel is below MIN_TRAVEL_STEPS: check sensors and wiring");
        return;
      }
      nextLegDir = DIR_CW;
      pauseStartMs = millis();
      state = S_PAUSE;
      break;

    case AS_LIMIT_HIT: {
      int32_t expected = (moveDir == DIR_CCW) ? travelSteps : 0;
      int32_t err = pos - expected;
      Serial.printf("%s limit hit at %ld steps (expected %ld, error %ld)\n",
                    dirName(moveDir), (long)pos, (long)expected, (long)err);
      if (absI(err) > ERROR_REZERO_STEPS) {
        stepper->setCurrentPosition(expected);
        Serial.printf("  error above %ld: position re-zeroed to %ld\n",
                      (long)ERROR_REZERO_STEPS, (long)expected);
      }
      nextLegDir = opposite(moveDir);
      pauseStartMs = millis();
      state = S_PAUSE;
      break;
    }

    default:
      fault("internal error: stopped with nothing to do");
      break;
  }
  afterStop = AS_NONE;
}

// Checks for the four moving states. Sensors have already been updated.
static void checkMotion() {
  Limit &expected = limitFor(moveDir);
  Limit &other = limitFor(opposite(moveDir));

  if (!other.blocked) {
    otherCleared = true;
  } else if (otherCleared || absI(stepper->getCurrentPosition() - legStartPos) > LEAVE_MAX_STEPS) {
    fault("wrong limit: check direction or sensor wiring");
    return;
  }

  if (expected.blocked) {
    switch (state) {
      case S_SEEK_CW:   stopAtLimit(AS_SEEK_CW_DONE); break;
      case S_MEASURE:   stopAtLimit(AS_MEASURE_DONE); break;
      case S_LEG:       stopAtLimit(AS_LIMIT_HIT);    break;
      default:          break;
    }
    return;
  }

  // Every move has a finite target, so the far sensor is always reached
  // within its step cap or the move ends here without it.
  if (moveFinished()) {
    fault("far sensor not reached within the step cap (MAX_SEEK_STEPS / LEG_OVERRUN_STEPS)");
  }
}

// ---- Serial ----

static void handleChar(char c) {
  switch (c) {
    case 's': userStop("STOP"); break;
    case 'd': userStop("Disable"); break;
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

// ---- Arduino entry points ----

void setup() {
  // Keep the driver disabled from the very first moment.
  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, HIGH);
  pinMode(PIN_STEP, OUTPUT);
  digitalWrite(PIN_STEP, LOW);
  pinMode(PIN_DIR, OUTPUT);
  digitalWrite(PIN_DIR, LOW);

  // Input-only pins: no internal pull-up exists, the sensor boards drive them.
  pinMode(PIN_LIMIT_CW, INPUT);
  pinMode(PIN_LIMIT_CCW, INPUT);

  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== 06 limit shuttle (UNLOADED motor, AUTO-STARTS) ===");

  bool driverOk = configureDriver();

  engine.init();
  stepper = engine.stepperConnectToPin(PIN_STEP);
  if (!stepper) {
    Serial.println("ERROR: could not attach stepper to STEP pin. Halting.");
    while (true) delay(1000);
  }
  // Position counts UP towards CCW, so DIR "counts up" when it is the CCW level.
  stepper->setDirectionPin(PIN_DIR, !CW_IS_DIR_HIGH);
  stepper->setEnablePin(PIN_EN);      // active low by default
  stepper->setAutoEnable(false);      // we enable/disable explicitly
  stepper->disableOutputs();
  stepper->setCurrentPosition(0);

  Serial.printf("Motor %u mA, %u microsteps, %ld steps/rev\n",
                MOTOR_RMS_CURRENT_MA, MICROSTEPS, (long)STEPS_PER_REV);
  Serial.printf("Clockwise = DIR %s (CW_IS_DIR_HIGH)\n", CW_IS_DIR_HIGH ? "HIGH" : "LOW");
  Serial.printf("Seek %lu Hz, leg %lu Hz, accel %lu, pause %lu ms\n",
                (unsigned long)SEEK_SPEED_HZ, (unsigned long)LEG_SPEED_HZ,
                (unsigned long)ACCEL_STEPS_S2,
                (unsigned long)PAUSE_MS);
  Serial.printf("Caps: seek %ld steps, leg overrun %ld, min travel %ld, rezero above %ld\n",
                (long)MAX_SEEK_STEPS, (long)LEG_OVERRUN_STEPS,
                (long)MIN_TRAVEL_STEPS, (long)ERROR_REZERO_STEPS);
  Serial.println("Sensors (raw state at boot):");
  printSensorLine(cwLimit);
  printSensorLine(ccwLimit);
  printHelp();

  if (!driverOk) {
    state = S_DRIVER_FAIL;
    Serial.println();
    Serial.println("Driver read-back FAILED: the motor will NOT be enabled. Fix the driver and reset.");
    return;
  }

  Serial.println();
  Serial.printf("Starting in %lu s, send 's' to abort\n", (unsigned long)COUNTDOWN_S);
  countdownStartMs = millis();
  state = S_COUNTDOWN;
}

void loop() {
  while (Serial.available()) handleChar((char)Serial.read());

  updateLimit(cwLimit);
  updateLimit(ccwLimit);

  switch (state) {
    case S_DRIVER_FAIL:
    case S_STOPPED:
    case S_FAULT:
      return;
    default:
      break;
  }

  // Both beams blocked at once can only be a wiring/polarity problem.
  if (cwLimit.blocked && ccwLimit.blocked) {
    fault("wrong limit: check direction or sensor wiring (both sensors blocked at once)");
    return;
  }

  switch (state) {
    case S_COUNTDOWN:
      if (millis() - countdownStartMs >= COUNTDOWN_S * 1000UL) startSeekCw();
      break;

    case S_SEEK_CW:
    case S_MEASURE:
    case S_LEG:
      checkMotion();
      break;

    case S_STOPPING:
      if (!stepper->isRunning()) {
        handleStopped();
      } else if (millis() - stopStartMs > 1000) {
        fault("motor did not stop after forceStop()");
      }
      break;

    case S_PAUSE:
      if (millis() - pauseStartMs >= PAUSE_MS) startLeg(nextLegDir);
      break;

    default:
      break;
  }
}
