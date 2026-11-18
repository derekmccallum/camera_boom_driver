// 07_web_shuttle
// Sketch 06's limit-to-limit ping-pong shuttle, controlled from a web page.
// The ESP32 joins your home WiFi (DHCP) and serves a page with:
//   - a big START / STOP button
//   - a slider for the shuttle speed (adjustable while it is moving)
//
// Behaviour:
//   - Boot: joins WiFi, then WAITS with the motor disabled. Nothing moves until
//     you press START (on the page, or send 'g' in the serial monitor).
//   - START (first time): seeks the CW sensor slowly, treats it as position 0,
//     measures the travel to the CCW sensor slowly, then shuttles between the two
//     sensors forever at the slider speed (accelerated, abrupt stop at each sensor).
//   - STOP: decelerates on the normal ramp and HOLDS position with the motor still
//     enabled. START then resumes what was interrupted, without re-homing.
//   - The slider changes the shuttle leg speed only (not the slow seek / measure
//     phases). The range is LEG_SPEED_MIN_HZ to LEG_SPEED_MAX_HZ in config.h.
//
// THIS MOVES THE MOTOR AS SOON AS SOMEONE PRESSES START. Keep the e-stop / power
// switch in reach, keep the boom area clear, and keep a physical stop in place.
// STOP on the web page needs a working WiFi link, so it is NOT an emergency stop.
// Serial monitor (115200 baud): 's' = STOP (same as the page), 'g' = start/resume,
// 'd' = hard stop and disable (reset the board to restart), '?' = status.
//
// STATUS: Tested on the boom: all features passed (START/STOP, resume, live speed
// changes, clockwise direction). Compiled for ESP32 Dev Module only. Report any
// compile errors or odd behaviour. Not yet confirmed: the AP fallback path.
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
//   do not replace a physical stop.
//
// POSITION CONVENTION: 0 = CW limit. Position counts UP towards the CCW limit.
// The CCW limit is at travelSteps.
//
// FAULTS (motor disabled, stays stopped until the board is reset):
//   - the sensor for the opposite direction trips, or both are blocked at once
//   - the far sensor is not reached within MAX_SEEK_STEPS (seek, measure) or
//     LEG_OVERRUN_STEPS past the expected limit position (shuttle legs)
//   - measured travel is below MIN_TRAVEL_STEPS
//   - the motor does not come to rest within HALT_TIMEOUT_MS of a STOP
//   - the driver read-back at boot does not match (motor never enabled)
//
// STRUCTURE: loop() runs the motion state machine and the limit sensors, exactly
// as in sketch 06. The web server runs in its own task on the other core, so a
// slow page load can never delay a sensor check. The web handlers only set
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

enum Dir { DIR_CW, DIR_CCW };

enum State {
  S_DRIVER_FAIL,   // driver read-back failed at boot: motor never enabled
  S_IDLE,          // motor disabled, waiting for START
  S_SEEK_CW,       // slow, towards the CW sensor
  S_MEASURE,       // slow, CW sensor to CCW sensor
  S_LEG,           // shuttle: run to the far sensor at the slider speed
  S_STOPPING,      // forceStop issued at a limit, waiting for the step queue to drain
  S_PAUSE,         // shuttle: pause at a limit
  S_HALTING,       // user STOP: decelerating
  S_HALTED,        // user STOP: at rest, motor enabled (holding), START resumes
  S_STOPPED,       // hard stop from serial 'd', disabled
  S_FAULT          // fault, disabled
};
static const char *STATE_NAMES[] = {
  "DRIVER_FAIL", "IDLE", "SEEK_CW", "MEASURE", "LEG",
  "STOPPING", "PAUSE", "HALTING", "HALTED", "STOPPED", "FAULT"
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

// Shared with the web task (written by one side, read by the other).
volatile State state = S_IDLE;
volatile int32_t travelSteps = 0;        // measured CW-to-CCW travel
volatile unsigned long legCount = 0;
const char *volatile faultReason = "";   // always points at a string literal
std::atomic<bool> startRequested(false);
std::atomic<bool> stopRequested(false);
std::atomic<uint32_t> legSpeedHz(LEG_SPEED_DEFAULT_HZ);

// Motion state machine, only touched by loop().
AfterStop afterStop = AS_NONE;
State resumeState = S_IDLE;     // what START resumes after a user STOP
Dir moveDir = DIR_CW;           // direction of the current leg / seek
Dir nextLegDir = DIR_CCW;       // direction of the next shuttle leg
int32_t legStartPos = 0;        // position when this leg began (leave check)
bool otherCleared = false;      // the sensor we are leaving has read clear
bool haltForced = false;        // a limit tripped during the STOP deceleration
uint32_t appliedLegHz = 0;      // speed the running leg was last given
unsigned long moveStartMs = 0;
unsigned long stopStartMs = 0;
unsigned long pauseStartMs = 0;
unsigned long haltStartMs = 0;

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
  Serial.println("  g  start / resume (same as the START button)");
  Serial.println("  s  STOP: decelerate and hold (same as the STOP button)");
  Serial.println("  d  hard stop and disable the motor (reset the board to restart)");
  Serial.println("  ?  state, position, sensors and driver diagnostics");
}

static void printStatus() {
  Serial.println();
  Serial.printf("state: %s  position: %ld steps  travelSteps: %ld  legs started: %lu  moving: %s  leg speed: %lu Hz\n",
                STATE_NAMES[state], (long)stepper->getCurrentPosition(),
                (long)travelSteps, (unsigned long)legCount,
                stepper->isRunning() ? "yes" : "no", (unsigned long)legSpeedHz.load());
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

// One shuttle leg: a single move at the slider speed with an acceleration ramp.
// The target is the far limit's expected position plus LEG_OVERRUN_STEPS, so the
// move is finite even if the sensor never trips. The sensor stops it earlier.
// A resumed leg (after a user STOP) is the same leg, so it is not counted again.
static void startLeg(Dir d, bool resumed) {
  beginLeg(d);
  if (!resumed) legCount = legCount + 1;
  appliedLegHz = legSpeedHz.load();
  int32_t target = (d == DIR_CCW) ? travelSteps + LEG_OVERRUN_STEPS : -LEG_OVERRUN_STEPS;
  Serial.printf("LEG %lu%s: %s at %lu Hz towards the %s sensor (expected at %ld)\n",
                (unsigned long)legCount, resumed ? " (resumed)" : "", dirName(d),
                (unsigned long)appliedLegHz, dirName(d),
                (long)((d == DIR_CCW) ? travelSteps : 0));
  if (startMoveTo(target, appliedLegHz)) state = S_LEG;
}

// Slider moved while a leg is running: apply the new speed on the ramp.
static void applyLegSpeedChange() {
  uint32_t want = legSpeedHz.load();
  if (want == appliedLegHz) return;
  appliedLegHz = want;
  stepper->setSpeedInHz(want);
  stepper->setAcceleration(ACCEL_STEPS_S2);
  stepper->applySpeedAcceleration();
  Serial.printf("Leg speed changed to %lu Hz\n", (unsigned long)want);
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

// Checks for the three moving states. Sensors have already been updated.
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

// ---- User STOP / START (requests come from the web task or the serial monitor) ----

// STOP while moving: decelerate on the ramp, then hold.
static void beginHalt() {
  resumeState = state;
  haltForced = false;
  haltStartMs = millis();
  stepper->stopMove();
  state = S_HALTING;
  Serial.println("STOP requested: decelerating");
}

// While decelerating after a STOP. A limit tripping on the way stops the motor at once.
static void checkHalting() {
  if (!haltForced && limitFor(moveDir).blocked) {
    stepper->forceStop();
    haltForced = true;
    Serial.printf("%s limit tripped while stopping: forced stop\n", dirName(moveDir));
  }
  // Same grace time as moveFinished(): isRunning() can lag just after a move starts.
  if (!stepper->isRunning() && millis() - haltStartMs > 30) {
    state = S_HALTED;
    Serial.printf("STOPPED at %ld steps. Motor still enabled (holding). START resumes.\n",
                  (long)stepper->getCurrentPosition());
  } else if (millis() - haltStartMs > HALT_TIMEOUT_MS) {
    fault("motor did not come to rest after STOP");
  }
}

// START after a user STOP: redo whatever was interrupted. If the target sensor
// is already blocked, go straight to the "limit reached" handling instead of moving.
static void resumeActivity() {
  Serial.println("START: resuming");
  switch (resumeState) {
    case S_SEEK_CW:
      startSeekCw();   // handles "already on the CW sensor"
      break;
    case S_MEASURE:
      if (ccwLimit.blocked) {
        afterStop = AS_MEASURE_DONE;
        stopStartMs = millis();
        state = S_STOPPING;
      } else {
        startMeasure();
      }
      break;
    case S_LEG:
      if (limitFor(moveDir).blocked) {
        afterStop = AS_LIMIT_HIT;
        stopStartMs = millis();
        state = S_STOPPING;
      } else {
        startLeg(moveDir, true);
      }
      break;
    case S_PAUSE:
      pauseStartMs = millis();   // the pause starts again
      state = S_PAUSE;
      break;
    default:
      fault("internal error: nothing to resume");
      break;
  }
}

static void processRequests() {
  if (startRequested.exchange(false)) {
    if (state == S_IDLE) {
      startSeekCw();
    } else if (state == S_HALTED) {
      resumeActivity();
    }
    // In any other state START means nothing and is dropped.
  }

  if (stopRequested.load()) {
    switch (state) {
      case S_STOPPING:
        return;   // a limit is being handled: keep the request pending until it is done
      case S_SEEK_CW:
      case S_MEASURE:
      case S_LEG:
        beginHalt();
        break;
      case S_PAUSE:
        resumeState = S_PAUSE;
        state = S_HALTED;
        Serial.println("STOPPED during the pause. START resumes.");
        break;
      default:
        break;    // idle, already stopping or stopped: nothing to do
    }
    stopRequested.store(false);
  }
}

// ---- Web ----

const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Boom Shuttle</title>
<style>
  body { font-family: sans-serif; text-align: center; margin: 1.5em; }
  #go { font-size: 3em; width: 100%; max-width: 480px; padding: 1em 0.5em; border: 0;
        border-radius: 0.3em; color: #fff; background: #888; }
  #go.start { background: #2a8a2a; }
  #go.stop  { background: #c62828; }
  #go:disabled { background: #999; }
  #speedbox { margin: 2em auto; max-width: 480px; }
  #speed { width: 100%; height: 3em; }
  #speedval { font-size: 2em; }
  #msg { font-size: 1.3em; margin: 1em 0; }
  #info { color: #555; }
</style></head><body>
<h1>Boom Shuttle</h1>
<button id="go" disabled>...</button>
<div id="msg">Connecting...</div>
<div id="speedbox">
  <div>Shuttle speed: <span id="speedval">-</span> steps/s</div>
  <input id="speed" type="range" min="0" max="1" step="10" value="0">
</div>
<div id="info"></div>
<script>
var st = null, dragging = false;
var go = document.getElementById('go'), msg = document.getElementById('msg');
var speed = document.getElementById('speed'), sv = document.getElementById('speedval');
var RUNNING = ['SEEK_CW', 'MEASURE', 'LEG', 'STOPPING', 'PAUSE'];
var TEXT = {
  IDLE: 'Ready. START homes to the CW sensor, measures the travel, then shuttles.',
  SEEK_CW: 'Homing: seeking the CW sensor...',
  MEASURE: 'Measuring travel to the CCW sensor...',
  LEG: 'Shuttling',
  STOPPING: 'Shuttling (at a limit)',
  PAUSE: 'Shuttling (paused at a limit)',
  HALTING: 'Stopping...',
  HALTED: 'Stopped (motor holding). START resumes.',
  STOPPED: 'Hard-stopped from serial. Reset the board.',
  FAULT: 'FAULT. Motor disabled. Reset the board.',
  DRIVER_FAIL: 'Driver check failed. Motor never enabled. Fix and reset the board.'
};
function render(s) {
  st = s;
  var running = RUNNING.indexOf(s.state) >= 0;
  go.className = '';
  go.disabled = false;
  if (running) { go.textContent = 'STOP'; go.className = 'stop'; }
  else if (s.state === 'IDLE' || s.state === 'HALTED') { go.textContent = 'START'; go.className = 'start'; }
  else { go.textContent = s.state === 'HALTING' ? 'STOPPING...' : s.state; go.disabled = true; }
  msg.textContent = (TEXT[s.state] || s.state) + (s.state === 'FAULT' && s.fault ? ' ' + s.fault : '');
  speed.min = s.min; speed.max = s.max;
  if (!dragging) { speed.value = s.speed; sv.textContent = s.speed; }
  document.getElementById('info').textContent =
    'Position ' + s.pos + ' / ' + s.travel + ' steps | legs ' + s.legs + ' | up ' + s.up + ' s';
}
function poll() {
  fetch('/status').then(function (r) { return r.json(); }).then(render)
    .catch(function () { msg.textContent = 'No connection to the boom...'; go.disabled = true; });
}
go.onclick = function () {
  if (!st) return;
  var running = RUNNING.indexOf(st.state) >= 0;
  go.disabled = true;
  fetch(running ? '/stop' : '/start').then(poll);
};
speed.addEventListener('pointerdown', function () { dragging = true; });
speed.addEventListener('input', function () { dragging = true; sv.textContent = speed.value; });
speed.addEventListener('change', function () {
  fetch('/speed?hz=' + speed.value).then(function () { dragging = false; poll(); });
});
setInterval(poll, 500);
poll();
</script></body></html>
)rawliteral";

static void sendStatus() {
  char buf[400];
  snprintf(buf, sizeof(buf),
           "{\"state\":\"%s\",\"pos\":%ld,\"travel\":%ld,\"legs\":%lu,"
           "\"speed\":%lu,\"min\":%lu,\"max\":%lu,\"up\":%lu,\"fault\":\"%s\"}",
           STATE_NAMES[state], (long)stepper->getCurrentPosition(), (long)travelSteps,
           (unsigned long)legCount, (unsigned long)legSpeedHz.load(),
           (unsigned long)LEG_SPEED_MIN_HZ, (unsigned long)LEG_SPEED_MAX_HZ,
           millis() / 1000, (const char *)faultReason);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", buf);
}

static void handleRoot()  { server.send_P(200, "text/html", PAGE); }
static void handleStart() { startRequested.store(true); sendStatus(); }
static void handleStop()  { stopRequested.store(true);  sendStatus(); }
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
  Serial.println("=== 07 web shuttle (UNLOADED motor, waits for START) ===");

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
  Serial.printf("Seek %lu Hz, leg %lu Hz (slider %lu to %lu), accel %lu, pause %lu ms\n",
                (unsigned long)SEEK_SPEED_HZ, (unsigned long)LEG_SPEED_DEFAULT_HZ,
                (unsigned long)LEG_SPEED_MIN_HZ, (unsigned long)LEG_SPEED_MAX_HZ,
                (unsigned long)ACCEL_STEPS_S2, (unsigned long)PAUSE_MS);
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
  server.on("/speed", handleSpeed);
  server.begin();
  xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, nullptr, 0);

  if (state != S_DRIVER_FAIL) {
    Serial.println();
    Serial.println("Ready. Motor disabled. Press START on the web page (or send 'g') to begin.");
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
    case S_LEG:
      applyLegSpeedChange();
      checkMotion();
      break;

    case S_SEEK_CW:
    case S_MEASURE:
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
      if (millis() - pauseStartMs >= PAUSE_MS) startLeg(nextLegDir, false);
      break;

    case S_HALTING:
      checkHalting();
      break;

    default:   // S_IDLE, S_HALTED: nothing moves
      break;
  }
}
