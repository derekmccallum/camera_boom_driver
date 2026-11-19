// 08_web_boundaries
// A self-calibrating web shuttle. On start-up the boom seeks BOTH optical sensors
// to learn its working area (so the sensors can be moved between sessions, e.g. to
// 110 degrees end to end), then goes HOME at the end chosen in config.h. The web
// page has:
//   - START / STOP: START begins a run (the boom sweeps between the two ends of
//     the working area, forever, pausing at each end). STOP decelerates and holds,
//     then the boom waits for the next command.
//   - a speed slider (live, 100 to 1600 steps/s by default; limits in config.h)
//   - RESET: seek both sensors again and set a new working area
//   - HOME: send the boom to the home position (also works during a run)
//
// Behaviour:
//   - Boot: configures the driver, joins WiFi (AP fallback), then (if
//     AUTO_CALIBRATE_ON_BOOT) counts down BOOT_CALIBRATE_DELAY_MS and starts the
//     calibration BY ITSELF. STOP on the page, or 's' on the serial monitor,
//     cancels the countdown; RESET (or 'r') then starts the calibration.
//   - Calibration (slow, SEEK_SPEED_HZ): seek the FAR sensor, then measure the
//     travel to the HOME sensor, then move to the home position, END_MARGIN_STEPS
//     inside the home sensor. START and HOME are disabled until this completes.
//   - Working area = sensor-to-sensor travel minus END_MARGIN_STEPS at each end.
//     Normal moves end with a smooth ramp inside it and never touch a sensor.
//   - A sensor that trips during a normal move is UNEXPECTED (lost steps, or a
//     sensor moved after calibration): the motor stops at once, the position is
//     re-synced to that sensor, and the boom waits. Press RESET after moving a
//     sensor.
//   - Position 0 is the home sensor; the position counts up towards the far end.
//     HOME_AT_CW_END in config.h picks which end is home; nothing else changes.
//
// THIS MOVES THE MOTOR BY ITSELF A FEW SECONDS AFTER BOOT. Keep the e-stop / power
// switch in reach, keep the boom area clear, and keep a physical stop in place.
// STOP and everything else on the web page needs a working WiFi link, so it is NOT
// an emergency stop.
// Serial monitor (115200 baud): 'g' = START, 's' = STOP, 'h' = HOME, 'r' = RESET
// (seek the boundaries again), 'd' = hard stop and disable (reset the board to
// restart), '?' = status.
//
// STATUS: Tested on the boom with the Adafruit TMC2209: it homed and responded
// correctly to all commands (chip version 0x21). Compiles for ESP32 Dev Module.
// Report any compile errors or odd behaviour. Not yet confirmed: the AP fallback.
//
// SETUP: copy secrets.example.h to secrets.h and fill in your network details.
//   cp secrets.example.h secrets.h
// The ESP32 supports 2.4 GHz WiFi only. The serial monitor prints the IP address.
// Open that address, or http://boom.local (mDNS: fine on macOS/iOS, often not
// on Android).
//
// WIRING (use the labels printed on each sensor PCB, not the cable plug order):
//   CW  sensor:  V -> ESP32 3V3 (NOT 5V),  G -> ESP32 GND,  S -> GPIO 34
//   CCW sensor:  V -> ESP32 3V3 (NOT 5V),  G -> ESP32 GND,  S -> GPIO 35
//   Driver as in sketch 05: STEP 25, DIR 26, EN 27 (active LOW),
//   UART RX 16 direct to PDN_UART, TX 17 via 1k to PDN_UART.
//   12 V on the driver BEFORE the ESP32 boots (the driver is configured once at boot).
//   An optical sensor that loses power just reads "clear": the soft checks here
//   do not replace a physical stop. Keep mechanical stops beyond the sensors.
//
// FAULTS (motor disabled, stays stopped until the board is reset):
//   - the sensor behind the direction of travel trips, or both are blocked at once
//   - a sensor is not reached within MAX_SEEK_STEPS (calibration)
//   - measured travel is below MIN_TRAVEL_STEPS
//   - the motor does not come to rest within HALT_TIMEOUT_MS of a STOP
//   - the driver read-back at boot does not match (motor never enabled)
//
// STRUCTURE: loop() runs the motion state machine and the limit sensors, exactly
// as in sketches 06 and 07. The web server runs in its own task on the other core,
// so a slow page load can never delay a sensor check. The web handlers only set
// request flags / the requested speed, and read a status snapshot.
//
// Libraries: TMCStepper, FastAccelStepper (Arduino Library Manager). WiFi,
// WebServer and ESPmDNS ship with the ESP32 core.

#include <atomic>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <TMCStepper.h>
#include <FastAccelStepper.h>
#include "config.h"
#include "secrets.h"

TMC2208Stepper driver(&Serial2, R_SENSE);
FastAccelStepperEngine engine;
FastAccelStepper *stepper = nullptr;
WebServer server(80);

// Directions are relative to the home end, not to CW/CCW. The position counts UP
// when moving AWAY from home. (HOME_AT_CW_END maps this onto the real direction.)
enum Dir { TOWARD_HOME, AWAY_FROM_HOME };

// DIR pin level that makes the position count up (= moving away from home).
// Away from home is CCW when home is the CW end, and CW when home is the CCW end.
constexpr bool COUNT_UP_IS_DIR_HIGH = HOME_AT_CW_END ? !CW_IS_DIR_HIGH : CW_IS_DIR_HIGH;

enum State {
  S_DRIVER_FAIL,   // driver read-back failed at boot: motor never enabled
  S_BOOT_WAIT,     // countdown to the automatic calibration, motor disabled
  S_IDLE,          // not calibrated, motor disabled: waiting for RESET
  S_CAL_FAR,       // calibration, slow: towards the far sensor
  S_CAL_MEASURE,   // calibration, slow: far sensor to home sensor
  S_MOVING,        // a move to a target inside the working area (see `purpose`)
  S_STOPPING,      // forceStop issued at a sensor, waiting for the step queue to drain
  S_PAUSE,         // run: pause at an end
  S_HALTING,       // user STOP: decelerating
  S_READY,         // calibrated, arrived, motor enabled (holding), waiting for a command
  S_HALTED,        // user STOP (or unexpected sensor trip): at rest, holding, waiting
  S_STOPPED,       // hard stop from serial 'd', disabled
  S_FAULT          // fault, disabled
};
static const char *STATE_NAMES[] = {
  "DRIVER_FAIL", "BOOT_WAIT", "IDLE", "CAL_FAR", "CAL_MEASURE", "MOVING",
  "STOPPING", "PAUSE", "HALTING", "READY", "HALTED", "STOPPED", "FAULT"
};

// Why the current S_MOVING move is happening.
enum MovePurpose { P_NONE, P_BACKOFF, P_HOME, P_RUN };
static const char *PURPOSE_NAMES[] = { "none", "backoff", "home", "run" };

// What to do once the motor has really stopped on a sensor.
enum AfterStop { AS_NONE, AS_FAR_FOUND, AS_HOME_FOUND, AS_TRIP };

// What to do once a user STOP has brought the motor to rest.
enum AfterHalt { AH_NONE, AH_HOME, AH_CALIBRATE };

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
Limit &homeLimit = HOME_AT_CW_END ? cwLimit : ccwLimit;
Limit &farLimit  = HOME_AT_CW_END ? ccwLimit : cwLimit;

// Shared with the web task (written by one side, read by the other).
volatile State state = S_IDLE;
volatile MovePurpose purpose = P_NONE;
volatile bool calibrated = false;
volatile int32_t travelSteps = 0;        // measured home sensor to far sensor
volatile unsigned long bootWaitStartMs = 0;
const char *volatile faultReason = "";   // always points at a string literal
const char *volatile notice = "";        // ditto; shown on the page, no quotes in it
std::atomic<bool> startRequested(false);
std::atomic<bool> stopRequested(false);
std::atomic<bool> homeRequested(false);
std::atomic<bool> resetRequested(false);
std::atomic<uint32_t> legSpeedHz(LEG_SPEED_DEFAULT_HZ);

// Motion state machine, only touched by loop().
AfterStop afterStop = AS_NONE;
AfterHalt afterHalt = AH_NONE;
Dir moveDir = AWAY_FROM_HOME;   // direction of the current move
Dir runDir = AWAY_FROM_HOME;    // direction of the current / next run leg
int32_t legStartPos = 0;        // position when this move began (leave check)
bool otherCleared = false;      // the sensor we are leaving has read clear
bool haltForced = false;        // a sensor tripped during the STOP deceleration
uint32_t appliedHz = 0;         // speed the running move was last given
unsigned long moveStartMs = 0;
unsigned long stopStartMs = 0;
unsigned long pauseStartMs = 0;
unsigned long haltStartMs = 0;

static Dir opposite(Dir d) { return d == TOWARD_HOME ? AWAY_FROM_HOME : TOWARD_HOME; }
static Limit &limitFor(Dir d) { return d == TOWARD_HOME ? homeLimit : farLimit; }
static int32_t absI(int32_t v) { return v < 0 ? -v : v; }

// Real-world name of the sensor in direction d ("CW" or "CCW").
static const char *endName(Dir d) {
  bool cw = (d == TOWARD_HOME) == HOME_AT_CW_END;
  return cw ? "CW" : "CCW";
}
static const char *dirText(Dir d) { return d == TOWARD_HOME ? "towards home" : "away from home"; }

// Working area, in steps from the home sensor. Home is END_MARGIN_STEPS inside it.
static int32_t homePos() { return END_MARGIN_STEPS; }
static int32_t farPos()  { return travelSteps - END_MARGIN_STEPS; }
static int32_t edgePos(Dir d) { return d == AWAY_FROM_HOME ? travelSteps : 0; }

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

static void printSensorLine(const Limit &l, const char *role) {
  bool high = digitalRead(l.pin) == HIGH;
  Serial.printf("  %-3s (GPIO %d, %s): raw %s -> %s   debounced: %s   (blocked = %s)\n",
                l.name, l.pin, role, high ? "HIGH" : "LOW",
                (high == l.blockedIsHigh) ? "BLOCKED" : "clear",
                l.blocked ? "BLOCKED" : "clear",
                l.blockedIsHigh ? "HIGH" : "LOW");
}

// ---- Help / status ----

static void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  g  START a run (same as the START button)");
  Serial.println("  s  STOP: decelerate and hold (same as the STOP button)");
  Serial.println("  h  HOME: go to the home position (same as the HOME button)");
  Serial.println("  r  RESET: seek both sensors again (same as the RESET button)");
  Serial.println("  d  hard stop and disable the motor (reset the board to restart)");
  Serial.println("  ?  state, position, sensors and driver diagnostics");
}

static void printStatus() {
  Serial.println();
  Serial.printf("state: %s  purpose: %s  calibrated: %s  position: %ld steps  moving: %s  speed: %lu Hz\n",
                STATE_NAMES[state], PURPOSE_NAMES[purpose], calibrated ? "yes" : "no",
                (long)stepper->getCurrentPosition(), stepper->isRunning() ? "yes" : "no",
                (unsigned long)legSpeedHz.load());
  if (calibrated) {
    Serial.printf("travel (sensor to sensor): %ld steps (~%.1f deg, estimate)  working area: %ld to %ld steps (~%.1f deg)\n",
                  (long)travelSteps, travelSteps / STEPS_PER_DEGREE,
                  (long)homePos(), (long)farPos(), (farPos() - homePos()) / STEPS_PER_DEGREE);
  }
  Serial.println("sensors:");
  printSensorLine(homeLimit, "home");
  printSensorLine(farLimit, "far");

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
  faultReason = reason;
  state = S_FAULT;
  Serial.println();
  Serial.printf("FAULT: %s\n", reason);
  Serial.printf("Motor DISABLED. Position %ld, travelSteps %ld. Reset the board to restart.\n",
                (long)stepper->getCurrentPosition(), (long)travelSteps);
}

// Serial 'd': hard stop and disable, terminal until reset.
static void hardDisable() {
  if (state == S_FAULT) {
    Serial.println("Already in FAULT: motor disabled. Reset the board to restart.");
    return;
  }
  haltAndDisable();
  if (state != S_DRIVER_FAIL) state = S_STOPPED;
  Serial.println("Disable: motor DISABLED. Reset the board to restart.");
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

// Record the start of a move in direction d.
static void beginLeg(Dir d) {
  moveDir = d;
  legStartPos = stepper->getCurrentPosition();
  // The sensor we are leaving may still be blocked; it must clear, and must not
  // trip again once it has.
  otherCleared = !limitFor(opposite(d)).blocked;
}

// Move to an absolute target inside the working area. The target is finite and
// always inside the sensors, so the move ends by itself on the ramp.
static void startGoto(int32_t target, uint32_t hz, MovePurpose p) {
  Dir d = target >= stepper->getCurrentPosition() ? AWAY_FROM_HOME : TOWARD_HOME;
  beginLeg(d);
  purpose = p;
  appliedHz = hz;
  if (startMoveTo(target, hz)) state = S_MOVING;
}

static void startRunLeg(Dir d) {
  runDir = d;
  int32_t target = (d == AWAY_FROM_HOME) ? farPos() : homePos();
  Serial.printf("RUN: %s (%s sensor side) at %lu Hz to position %ld\n", dirText(d), endName(d),
                (unsigned long)legSpeedHz.load(), (long)target);
  startGoto(target, legSpeedHz.load(), P_RUN);
}

static void startHome() {
  runDir = AWAY_FROM_HOME;   // the next START sweeps away from home
  Serial.printf("HOME: to position %ld at %lu Hz\n", (long)homePos(), (unsigned long)legSpeedHz.load());
  startGoto(homePos(), legSpeedHz.load(), P_HOME);
}

// Calibration step 2: from the far sensor towards the home sensor, slowly.
static void startMeasure() {
  beginLeg(TOWARD_HOME);
  Serial.printf("MEASURE: %s at %lu Hz to the home (%s) sensor (max %ld steps)\n",
                dirText(TOWARD_HOME), (unsigned long)SEEK_SPEED_HZ, endName(TOWARD_HOME),
                (long)MAX_SEEK_STEPS);
  if (startMoveTo(stepper->getCurrentPosition() - MAX_SEEK_STEPS, SEEK_SPEED_HZ)) state = S_CAL_MEASURE;
}

// Calibration step 1: seek the far sensor, slowly. Position is set to 0 here, as
// a temporary origin; it is zeroed again at the home sensor.
static void startCalibration() {
  calibrated = false;
  purpose = P_NONE;
  travelSteps = 0;
  notice = "";
  afterHalt = AH_NONE;
  stepper->enableOutputs();
  stepper->setCurrentPosition(0);
  Serial.println();
  Serial.println("CALIBRATE: Motor ENABLED. Seeking both sensors to find the working area.");
  if (farLimit.blocked) {
    Serial.printf("  far (%s) sensor already blocked, treating it as found.\n", endName(AWAY_FROM_HOME));
    startMeasure();
    return;
  }
  beginLeg(AWAY_FROM_HOME);
  Serial.printf("SEEK: %s at %lu Hz to the far (%s) sensor (max %ld steps)\n",
                dirText(AWAY_FROM_HOME), (unsigned long)SEEK_SPEED_HZ, endName(AWAY_FROM_HOME),
                (long)MAX_SEEK_STEPS);
  if (startMoveTo(MAX_SEEK_STEPS, SEEK_SPEED_HZ)) state = S_CAL_FAR;
}

// Slider moved while a run or home move is going: apply the new speed on the ramp.
static void applySpeedChange() {
  if (purpose != P_RUN && purpose != P_HOME) return;
  uint32_t want = legSpeedHz.load();
  if (want == appliedHz) return;
  appliedHz = want;
  stepper->setSpeedInHz(want);
  stepper->setAcceleration(ACCEL_STEPS_S2);
  stepper->applySpeedAcceleration();
  Serial.printf("Speed changed to %lu Hz\n", (unsigned long)want);
}

// A sensor tripped: stop abruptly, then handle it once the queue drains.
static void stopAtLimit(AfterStop what) {
  stepper->forceStop();
  afterStop = what;
  stopStartMs = millis();
  state = S_STOPPING;
}

// The motor stopped on a sensor during a normal move: set the position to that
// sensor's known position if it is off by more than ERROR_REZERO_STEPS.
static void resyncToEdge(Dir d, int32_t pos) {
  int32_t expected = edgePos(d);
  int32_t err = pos - expected;
  Serial.printf("  %s sensor at %ld steps (expected %ld, error %ld)\n",
                endName(d), (long)pos, (long)expected, (long)err);
  if (absI(err) > ERROR_REZERO_STEPS) {
    stepper->setCurrentPosition(expected);
    Serial.printf("  error above %ld: position re-zeroed to %ld\n",
                  (long)ERROR_REZERO_STEPS, (long)expected);
  }
}

// Motor has really stopped on a sensor (position is valid now).
static void handleStopped() {
  int32_t pos = stepper->getCurrentPosition();
  AfterStop what = afterStop;
  afterStop = AS_NONE;
  switch (what) {
    case AS_FAR_FOUND:
      stepper->setCurrentPosition(0);
      Serial.printf("Far (%s) sensor found. Temporary origin set.\n", endName(AWAY_FROM_HOME));
      startMeasure();
      break;

    case AS_HOME_FOUND: {
      int32_t travel = -pos;    // moved towards home, so the position went negative
      Serial.printf("Home (%s) sensor found. Travel = %ld steps (%.2f rev of the bare motor, ~%.1f deg at the estimated ratio)\n",
                    endName(TOWARD_HOME), (long)travel, (float)travel / STEPS_PER_REV,
                    travel / STEPS_PER_DEGREE);
      if (travel < MIN_TRAVEL_STEPS) {
        fault("measured travel is below MIN_TRAVEL_STEPS: check sensors and wiring");
        return;
      }
      stepper->setCurrentPosition(0);   // 0 = the home sensor
      travelSteps = travel;
      calibrated = true;
      runDir = AWAY_FROM_HOME;
      Serial.printf("Working area: %ld to %ld steps (~%.1f deg). Moving to home.\n",
                    (long)homePos(), (long)farPos(), (farPos() - homePos()) / STEPS_PER_DEGREE);
      startGoto(homePos(), SEEK_SPEED_HZ, P_BACKOFF);
      break;
    }

    case AS_TRIP:
      Serial.printf("UNEXPECTED: the %s sensor tripped during a move %s. Stopped.\n",
                    endName(moveDir), dirText(moveDir));
      resyncToEdge(moveDir, pos);
      purpose = P_NONE;
      runDir = opposite(moveDir);
      notice = "A sensor tripped inside the working area (lost steps, or a sensor was moved). Position re-synced. Press RESET if you moved a sensor.";
      state = S_HALTED;
      break;

    default:
      fault("internal error: stopped with nothing to do");
      break;
  }
}

// A normal move reached its target.
static void arrived() {
  MovePurpose p = purpose;
  purpose = P_NONE;
  Serial.printf("Arrived at %ld steps.\n", (long)stepper->getCurrentPosition());
  switch (p) {
    case P_BACKOFF:
      Serial.println("Calibration complete. At home. Motor holding. START runs, HOME returns here.");
      state = S_READY;
      break;
    case P_HOME:
      Serial.println("At home. Motor holding.");
      state = S_READY;
      break;
    case P_RUN:
      runDir = opposite(moveDir);
      pauseStartMs = millis();
      state = S_PAUSE;
      break;
    default:
      fault("internal error: arrived with no purpose");
      break;
  }
}

// Checks for the moving states. Sensors have already been updated.
static void checkMotion() {
  Limit &ahead = limitFor(moveDir);
  Limit &behind = limitFor(opposite(moveDir));

  if (!behind.blocked) {
    otherCleared = true;
  } else if (otherCleared || absI(stepper->getCurrentPosition() - legStartPos) > LEAVE_MAX_STEPS) {
    fault("wrong limit: check direction or sensor wiring");
    return;
  }

  if (ahead.blocked) {
    switch (state) {
      case S_CAL_FAR:     stopAtLimit(AS_FAR_FOUND);  break;
      case S_CAL_MEASURE: stopAtLimit(AS_HOME_FOUND); break;
      case S_MOVING:      stopAtLimit(AS_TRIP);       break;
      default:            break;
    }
    return;
  }

  if (moveFinished()) {
    switch (state) {
      case S_MOVING:
        arrived();
        break;
      case S_CAL_FAR:
        fault("far sensor not reached within MAX_SEEK_STEPS");
        break;
      case S_CAL_MEASURE:
        fault("home sensor not reached within MAX_SEEK_STEPS");
        break;
      default:
        break;
    }
  }
}

// ---- User commands (requests come from the web task or the serial monitor) ----

// STOP while moving: decelerate on the ramp, then hold.
static void beginHalt() {
  haltForced = false;
  haltStartMs = millis();
  stepper->stopMove();
  state = S_HALTING;
  Serial.println("STOP: decelerating");
}

// While decelerating after a STOP. A sensor tripping on the way stops the motor at once.
static void checkHalting() {
  if (!haltForced && limitFor(moveDir).blocked) {
    stepper->forceStop();
    haltForced = true;
    Serial.printf("%s sensor tripped while stopping: forced stop\n", endName(moveDir));
  }
  // Same grace time as moveFinished(): isRunning() can lag just after a move starts.
  if (!stepper->isRunning() && millis() - haltStartMs > 30) {
    // A forced stop during a normal move means the position may be off.
    if (haltForced && calibrated && purpose != P_NONE) {
      resyncToEdge(moveDir, stepper->getCurrentPosition());
    }
    purpose = P_NONE;
    state = S_HALTED;
    Serial.printf("STOPPED at %ld steps. Motor still enabled (holding). Waiting for a command.\n",
                  (long)stepper->getCurrentPosition());

    AfterHalt next = afterHalt;
    afterHalt = AH_NONE;
    if (next == AH_CALIBRATE) {
      startCalibration();
    } else if (next == AH_HOME && calibrated) {
      startHome();
    }
  } else if (millis() - haltStartMs > HALT_TIMEOUT_MS) {
    fault("motor did not come to rest after STOP");
  }
}

static bool isMovingState() {
  return state == S_CAL_FAR || state == S_CAL_MEASURE || state == S_MOVING;
}

static bool isStationaryState() {
  return state == S_READY || state == S_HALTED;
}

static void doStop() {
  if (state == S_BOOT_WAIT) {
    state = S_IDLE;
    Serial.println("STOP: automatic calibration cancelled. Press RESET to calibrate.");
  } else if (isMovingState()) {
    afterHalt = AH_NONE;
    beginHalt();
  } else if (state == S_PAUSE) {
    state = S_HALTED;
    Serial.println("STOPPED during the pause. Waiting for a command.");
  } else if (state == S_HALTING) {
    afterHalt = AH_NONE;    // a second STOP cancels a queued HOME / RESET
  }
}

static void doReset() {
  if (state == S_BOOT_WAIT || state == S_IDLE || state == S_PAUSE || isStationaryState()) {
    startCalibration();
  } else if (isMovingState()) {
    afterHalt = AH_CALIBRATE;
    beginHalt();
  } else if (state == S_HALTING) {
    afterHalt = AH_CALIBRATE;
  }
}

static void doHome() {
  if (!calibrated) {
    Serial.println("HOME ignored: boundaries not known yet. Use RESET.");
    return;
  }
  if (isStationaryState() || state == S_PAUSE) {
    startHome();
  } else if (state == S_MOVING && purpose == P_RUN) {
    afterHalt = AH_HOME;
    beginHalt();
  } else if (state == S_HALTING) {
    afterHalt = AH_HOME;
  }
  // Anything else (already homing, calibrating): nothing to do.
}

static void doStart() {
  if (!calibrated) {
    Serial.println("START ignored: boundaries not known yet. Use RESET.");
    return;
  }
  if (isStationaryState()) {
    notice = "";
    startRunLeg(runDir);
  }
  // Any other state: START means nothing and is dropped.
}

static void processRequests() {
  // While a sensor trip is being handled, requests stay pending until it is done.
  if (state == S_STOPPING) return;
  // STOP is handled first, so it always wins over anything that arrived with it.
  if (stopRequested.exchange(false))  doStop();
  if (resetRequested.exchange(false)) doReset();
  if (homeRequested.exchange(false))  doHome();
  if (startRequested.exchange(false)) doStart();
}

// ---- Web ----

const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Boom</title>
<style>
  body { font-family: sans-serif; text-align: center; margin: 1.5em; }
  button { border: 0; border-radius: 0.3em; color: #fff; background: #888; }
  button:disabled { background: #bbb; }
  #go { font-size: 3em; width: 100%; max-width: 480px; padding: 1em 0.5em; }
  #go.start { background: #2a8a2a; }
  #go.stop  { background: #c62828; }
  #row { margin: 1em auto; max-width: 480px; display: flex; gap: 1em; }
  #row button { flex: 1; font-size: 1.6em; padding: 0.8em 0.2em; }
  #home { background: #1565c0; }
  #reset { background: #ef6c00; }
  #speedbox { margin: 2em auto; max-width: 480px; }
  #speed { width: 100%; height: 3em; }
  #speedval { font-size: 2em; }
  #msg { font-size: 1.3em; margin: 1em 0; min-height: 2.6em; }
  #notice { color: #b71c1c; margin: 0.5em 0; }
  #info { color: #555; line-height: 1.5; }
</style></head><body>
<h1>Boom</h1>
<div id="msg">Connecting...</div>
<button id="go" disabled>...</button>
<div id="row">
  <button id="home" disabled>HOME</button>
  <button id="reset" disabled>RESET</button>
</div>
<div id="speedbox">
  <div>Speed: <span id="speedval">-</span> steps/s <span id="degs"></span></div>
  <input id="speed" type="range" min="0" max="1" step="10" value="0">
</div>
<div id="notice"></div>
<div id="info"></div>
<script>
var st = null, dragging = false;
var go = document.getElementById('go'), msg = document.getElementById('msg');
var homeBtn = document.getElementById('home'), resetBtn = document.getElementById('reset');
var speed = document.getElementById('speed'), sv = document.getElementById('speedval');
var degs = document.getElementById('degs');
var RUNNING = ['BOOT_WAIT', 'CAL_FAR', 'CAL_MEASURE', 'MOVING', 'STOPPING', 'PAUSE'];
var CAN_HOME = ['READY', 'HALTED', 'MOVING', 'PAUSE'];
var DEAD = ['FAULT', 'STOPPED', 'DRIVER_FAIL'];
var TEXT = {
  IDLE: 'Boundaries not known. Press RESET to seek both sensors.',
  HALTING: 'Stopping...',
  READY: 'At rest, holding. START runs, HOME returns home.',
  HALTED: 'Stopped (motor holding). Waiting for a command.',
  PAUSE: 'Running (paused at an end)',
  STOPPING: 'A sensor tripped, stopping...',
  STOPPED: 'Hard-stopped from serial. Reset the board.',
  FAULT: 'FAULT. Motor disabled. Reset the board.',
  DRIVER_FAIL: 'Driver check failed. Motor never enabled. Fix and reset the board.'
};
function deg(steps, s) { return (steps / s.spd).toFixed(1); }
function text(s) {
  if (s.state === 'BOOT_WAIT') return 'Calibrating by itself in ' + s.count + ' s. STOP cancels.';
  if (s.state === 'CAL_FAR') return 'Calibrating: seeking the far (' + s.far + ') sensor...';
  if (s.state === 'CAL_MEASURE') return 'Calibrating: measuring to the home (' + s.home + ') sensor...';
  if (s.state === 'MOVING') {
    if (s.purpose === 'run') return 'Running';
    if (s.purpose === 'home') return 'Going home';
    return 'Moving to home';
  }
  var t = TEXT[s.state] || s.state;
  if (s.state === 'FAULT' && s.fault) t += ' ' + s.fault;
  return t;
}
function render(s) {
  st = s;
  var running = RUNNING.indexOf(s.state) >= 0;
  var dead = DEAD.indexOf(s.state) >= 0;
  var at = s.state === 'READY' || s.state === 'HALTED';
  go.className = '';
  go.disabled = false;
  if (running) { go.textContent = 'STOP'; go.className = 'stop'; }
  else if (at && s.cal) { go.textContent = 'START'; go.className = 'start'; }
  else { go.textContent = s.state === 'HALTING' ? 'STOPPING...' : 'START'; go.disabled = true; }
  homeBtn.disabled = !(s.cal && CAN_HOME.indexOf(s.state) >= 0);
  resetBtn.disabled = dead;
  msg.textContent = text(s);
  speed.min = s.min; speed.max = s.max;
  if (!dragging) { speed.value = s.speed; sv.textContent = s.speed; }
  degs.textContent = '(about ' + deg(speed.value, s) + ' deg/s)';
  document.getElementById('notice').textContent = s.notice;
  var info = 'Home: ' + s.home + ' end | up ' + s.up + ' s';
  if (s.cal) {
    info = 'Working area ' + s.span + ' steps (about ' + deg(s.span, s) + ' deg), sensor to sensor ' +
      s.travel + ' steps (about ' + deg(s.travel, s) + ' deg)<br>' +
      'Position from home: ' + s.pos + ' steps (about ' + deg(s.pos, s) + ' deg)<br>' + info;
  }
  document.getElementById('info').innerHTML = info +
    '<br><small>Degrees use an ESTIMATED steps-per-degree, not yet calibrated.</small>';
}
function poll() {
  fetch('/status').then(function (r) { return r.json(); }).then(render)
    .catch(function () {
      msg.textContent = 'No connection to the boom...';
      go.disabled = true; homeBtn.disabled = true; resetBtn.disabled = true;
    });
}
function cmd(url) {
  go.disabled = true; homeBtn.disabled = true; resetBtn.disabled = true;
  fetch(url).then(poll, poll);
}
go.onclick = function () {
  if (!st) return;
  cmd(RUNNING.indexOf(st.state) >= 0 ? '/stop' : '/start');
};
homeBtn.onclick = function () { cmd('/home'); };
resetBtn.onclick = function () {
  if (confirm('RESET: the boom will move slowly to both ends to find its boundaries again. Continue?')) cmd('/reset');
};
speed.addEventListener('pointerdown', function () { dragging = true; });
speed.addEventListener('input', function () {
  dragging = true; sv.textContent = speed.value;
  if (st) degs.textContent = '(about ' + deg(speed.value, st) + ' deg/s)';
});
speed.addEventListener('change', function () {
  fetch('/speed?hz=' + speed.value).then(function () { dragging = false; poll(); });
});
setInterval(poll, 500);
poll();
</script></body></html>
)rawliteral";

static void sendStatus() {
  int32_t pos = stepper->getCurrentPosition();
  bool cal = calibrated;
  long countdown = 0;
  if (state == S_BOOT_WAIT) {
    unsigned long elapsed = millis() - bootWaitStartMs;
    countdown = elapsed >= BOOT_CALIBRATE_DELAY_MS ? 0 : (long)((BOOT_CALIBRATE_DELAY_MS - elapsed) / 1000 + 1);
  }
  char buf[900];
  snprintf(buf, sizeof(buf),
           "{\"state\":\"%s\",\"purpose\":\"%s\",\"cal\":%d,\"pos\":%ld,\"travel\":%ld,\"span\":%ld,"
           "\"speed\":%lu,\"min\":%lu,\"max\":%lu,\"spd\":%.2f,\"home\":\"%s\",\"far\":\"%s\","
           "\"count\":%ld,\"up\":%lu,\"fault\":\"%s\",\"notice\":\"%s\"}",
           STATE_NAMES[state], PURPOSE_NAMES[purpose], cal ? 1 : 0,
           (long)(pos - homePos()), (long)travelSteps, cal ? (long)(farPos() - homePos()) : 0L,
           (unsigned long)legSpeedHz.load(), (unsigned long)LEG_SPEED_MIN_HZ,
           (unsigned long)LEG_SPEED_MAX_HZ, (double)STEPS_PER_DEGREE,
           endName(TOWARD_HOME), endName(AWAY_FROM_HOME),
           countdown, millis() / 1000, (const char *)faultReason, (const char *)notice);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", buf);
}

static void handleRoot()  { server.send_P(200, "text/html", PAGE); }
static void handleStart() { startRequested.store(true); sendStatus(); }
static void handleStop()  { stopRequested.store(true);  sendStatus(); }
static void handleHome()  { homeRequested.store(true);  sendStatus(); }
static void handleReset() { resetRequested.store(true); sendStatus(); }
static void handleSpeed() {
  if (!server.hasArg("hz")) {
    server.send(400, "text/plain", "missing hz");
    return;
  }
  long hz = server.arg("hz").toInt();
  if (hz < (long)LEG_SPEED_MIN_HZ) hz = LEG_SPEED_MIN_HZ;
  if (hz > (long)LEG_SPEED_MAX_HZ) hz = LEG_SPEED_MAX_HZ;
  legSpeedHz.store((uint32_t)hz);
  sendStatus();
}

// The web server lives in its own task so that serving a page never delays the
// limit sensor checks in loop(). It is the only code that touches `server`.
static void webTask(void *) {
  for (;;) {
    server.handleClient();
    vTaskDelay(1);
  }
}

// Join the home network and wait for a DHCP lease. Returns true on success.
static bool startStation() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Connecting to '%s'", WIFI_SSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("Could not join '%s' (status %d).\n", WIFI_SSID, (int)WiFi.status());
    return false;
  }
  Serial.print("Connected. DHCP address: http://");
  Serial.println(WiFi.localIP());
  Serial.printf("Signal: %d dBm\n", WiFi.RSSI());
  return true;
}

static void startAccessPoint() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.printf("Fallback AP started. Join '%s' and browse to http://", AP_SSID);
  Serial.println(WiFi.softAPIP());   // normally 192.168.4.1
}

// ---- Serial ----

static void handleChar(char c) {
  switch (c) {
    case 'g': startRequested.store(true); break;
    case 's': stopRequested.store(true); break;
    case 'h': homeRequested.store(true); break;
    case 'r': resetRequested.store(true); break;
    case 'd': hardDisable(); break;
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
  Serial.println("=== 08 web boundaries (self-calibrating shuttle) ===");

  bool driverOk = configureDriver();

  engine.init();
  stepper = engine.stepperConnectToPin(PIN_STEP);
  if (!stepper) {
    Serial.println("ERROR: could not attach stepper to STEP pin. Halting.");
    while (true) delay(1000);
  }
  // Position counts UP when moving away from home (see COUNT_UP_IS_DIR_HIGH).
  stepper->setDirectionPin(PIN_DIR, COUNT_UP_IS_DIR_HIGH);
  stepper->setEnablePin(PIN_EN);      // active low by default
  stepper->setAutoEnable(false);      // we enable/disable explicitly
  stepper->disableOutputs();
  stepper->setCurrentPosition(0);

  Serial.printf("Motor %u mA, %u microsteps, %ld steps/rev, ~%.1f steps/degree (ESTIMATE, display only)\n",
                MOTOR_RMS_CURRENT_MA, MICROSTEPS, (long)STEPS_PER_REV, STEPS_PER_DEGREE);
  Serial.printf("Clockwise = DIR %s (CW_IS_DIR_HIGH). Home end: %s (%s sensor, GPIO %d), far end: %s (GPIO %d)\n",
                CW_IS_DIR_HIGH ? "HIGH" : "LOW", HOME_AT_CW_END ? "CW" : "CCW",
                homeLimit.name, homeLimit.pin, farLimit.name, farLimit.pin);
  Serial.printf("Seek %lu Hz, run speed %lu Hz (slider %lu to %lu), accel %lu, pause %lu ms\n",
                (unsigned long)SEEK_SPEED_HZ, (unsigned long)LEG_SPEED_DEFAULT_HZ,
                (unsigned long)LEG_SPEED_MIN_HZ, (unsigned long)LEG_SPEED_MAX_HZ,
                (unsigned long)ACCEL_STEPS_S2, (unsigned long)PAUSE_MS);
  Serial.printf("End margin %ld steps, seek cap %ld steps, min travel %ld, rezero above %ld\n",
                (long)END_MARGIN_STEPS, (long)MAX_SEEK_STEPS,
                (long)MIN_TRAVEL_STEPS, (long)ERROR_REZERO_STEPS);
  Serial.println("Sensors (raw state at boot):");
  printSensorLine(homeLimit, "home");
  printSensorLine(farLimit, "far");
  printHelp();

  if (!driverOk) {
    state = S_DRIVER_FAIL;
    Serial.println();
    Serial.println("Driver read-back FAILED: the motor will NOT be enabled. Fix the driver and reset.");
  }

  // WiFi last, so the motor hardware is already safe while it connects.
  Serial.println();
  if (!startStation()) {
    if (ENABLE_AP_FALLBACK) {
      startAccessPoint();
    } else {
      Serial.println("No network and AP fallback disabled. The web page is unreachable.");
    }
  }
  if (MDNS.begin(HOSTNAME)) {
    Serial.printf("Also try http://%s.local\n", HOSTNAME);
  }

  server.on("/", handleRoot);
  server.on("/status", sendStatus);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/home", handleHome);
  server.on("/reset", handleReset);
  server.on("/speed", handleSpeed);
  server.begin();
  xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, nullptr, 0);

  if (state != S_DRIVER_FAIL) {
    Serial.println();
    if (AUTO_CALIBRATE_ON_BOOT) {
      bootWaitStartMs = millis();
      state = S_BOOT_WAIT;
      Serial.printf("The boom will START MOVING BY ITSELF in %lu s to seek both sensors. "
                    "Press STOP on the page (or send 's') to cancel.\n",
                    BOOT_CALIBRATE_DELAY_MS / 1000);
    } else {
      state = S_IDLE;
      Serial.println("Ready. Motor disabled. Press RESET on the web page (or send 'r') to seek the sensors.");
    }
  }
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

  processRequests();

  switch (state) {
    case S_BOOT_WAIT:
      if (millis() - bootWaitStartMs >= BOOT_CALIBRATE_DELAY_MS) startCalibration();
      break;

    case S_MOVING:
      applySpeedChange();
      checkMotion();
      break;

    case S_CAL_FAR:
    case S_CAL_MEASURE:
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
      if (millis() - pauseStartMs >= PAUSE_MS) startRunLeg(runDir);
      break;

    case S_HALTING:
      checkHalting();
      break;

    default:   // S_IDLE, S_READY, S_HALTED: nothing moves
      break;
  }
}
