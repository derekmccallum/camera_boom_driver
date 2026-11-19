// config.h - pins and tunables for sketch 08 (self-calibrating web shuttle).
// Tested on the boom (sketch 08 passed). R_SENSE below is still NOT verified.
// Sensor polarity was measured on the bench: both active-HIGH (see below).
// Network name and passwords live in secrets.h (gitignored).
#pragma once

// ---- Motor driver pins (same as sketches 04 to 07) ----
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
constexpr uint16_t MOTOR_RMS_CURRENT_MA = 400;   // as in sketch 07 (passed on the boom)
constexpr uint16_t MICROSTEPS           = 16;
constexpr uint16_t MOTOR_FULL_STEPS_REV = 200;   // 1.8 degree motor
constexpr int32_t  STEPS_PER_REV = (int32_t)MOTOR_FULL_STEPS_REV * MICROSTEPS;  // 3200

// ESTIMATE ONLY, from the designed ~16:1 ratio: 3200 * 16 / 360. Used for DISPLAY
// ONLY (degrees on the web page). Every move is in steps, so a wrong value here
// cannot make the boom move further than it should. Calibrate on the real arm:
// measure the end-to-end angle the page reports against a real protractor reading.
constexpr float STEPS_PER_DEGREE = 142.2f;

// ---- Limit sensors (BIQU optical endstops, 3.3 V, input-only pins, no pull-up) ----
// CW limit = the end reached by turning clockwise, viewed from above.
constexpr int PIN_LIMIT_CW  = 34;
constexpr int PIN_LIMIT_CCW = 35;

// Bench-measured with sketch 06's '?' command: with this wiring BOTH sensors read
// HIGH when the beam is blocked and LOW when it is clear (active-HIGH), and the
// sensor LED is lit when blocked. Check with '?' (raw levels) if a sensor is ever
// swapped or rewired.
constexpr bool CW_BLOCKED_IS_HIGH  = true;
constexpr bool CCW_BLOCKED_IS_HIGH = true;

// ---- Direction ----
// Clockwise is viewed from above, looking down at the boom's pivot. Which DIR
// level turns the boom clockwise depends on how the motor is mounted (tested on
// the boom as built). If it is ever wrong, the boom reaches the other sensor first
// and the sketch stops with "wrong limit".
constexpr bool CW_IS_DIR_HIGH = true;

// ---- HOME END ----
// Which end of the working area is HOME. true = the clockwise (CW) end,
// false = the counter-clockwise (CCW) end. Position 0 is the home sensor; the
// position counts up towards the far end. This also decides which way START
// sweeps first (away from home) and where the HOME button sends the boom.
constexpr bool HOME_AT_CW_END = true;

// ---- Working area ----
// The two optical sensors define the boundaries of the area. The boom never runs
// into a sensor during normal moves: the working area is the sensor-to-sensor
// travel minus this margin at EACH end, so every move ends with a smooth ramp
// and the sensors stay a safety trip. Home is END_MARGIN_STEPS from the home
// sensor. At the estimated 142 steps/degree, 100 steps is about 0.7 degree, so
// sensors set 110 degrees apart give about 108.6 degrees of working area.
// It must cover the sensor-edge position error from the calibration (a few steps
// at the slow seek speed). Too small and the boom can false-trip at the ends.
constexpr int32_t END_MARGIN_STEPS = 100;

// ---- Start-up ----
// Seek both ends automatically after boot (after WiFi has connected) and wait
// BOOT_CALIBRATE_DELAY_MS first, so you can press STOP on the page or send 's' to
// cancel. THE BOOM MOVES BY ITSELF WHEN THIS IS true. If false, nothing moves
// until RESET is pressed on the page (or 'r' is sent on the serial monitor).
constexpr bool AUTO_CALIBRATE_ON_BOOT = true;
constexpr unsigned long BOOT_CALIBRATE_DELAY_MS = 5000;

// ---- Motion (units: microsteps/s and microsteps/s^2) ----
// Slow speed for seeking the two ends and for the first move to home (not adjustable).
constexpr uint32_t SEEK_SPEED_HZ   = 400;

// Run and HOME speed, adjustable live from the web slider. Ends of normal moves
// are smooth ramps (not sensor stops), but the sensor stop in an unexpected trip
// is abrupt and its overshoot grows with speed. Keep the maximum modest until the
// loaded boom has been tested at higher speed (torque falls with speed, and the
// motor is at 400 mA). The server clamps any request into this range.
constexpr uint32_t LEG_SPEED_MIN_HZ     = 100;
constexpr uint32_t LEG_SPEED_MAX_HZ     = 1600;
constexpr uint32_t LEG_SPEED_DEFAULT_HZ = 400;

constexpr uint32_t ACCEL_STEPS_S2  = 1600;   // used on every move (also the STOP deceleration)
constexpr unsigned long PAUSE_MS   = 1000;   // pause at each end of a run before reversing

// ---- Sensor debounce ----
// A sensor changes state only after DEBOUNCE_COUNT consecutive samples that
// agree, taken at least SENSOR_SAMPLE_US apart. 3 x 1000 us = at least 2 ms
// between the first and the last sample. (Sensors are read on every loop();
// only reads that are SENSOR_SAMPLE_US apart count towards the streak.)
constexpr uint8_t  DEBOUNCE_COUNT   = 3;
constexpr unsigned long SENSOR_SAMPLE_US = 1000;

// ---- Safety limits ----
// The far sensor (first seek) and then the home sensor (measure) must be reached
// within this many steps. The estimated full range is about 17,000 steps for 120
// degrees, so 30,000 is about 210 degrees at the estimated ratio. Sketch 07 used
// 64,000. Raise it if the real arm needs more.
constexpr int32_t MAX_SEEK_STEPS = 30000;
// Measured travel below this is a fault (a sensor is probably stuck or mis-wired).
constexpr int32_t MIN_TRAVEL_STEPS = 800;
static_assert(MIN_TRAVEL_STEPS > 2 * END_MARGIN_STEPS,
              "MIN_TRAVEL_STEPS must leave a working area after the two end margins");
// When a move starts with the sensor we are leaving still blocked, it must clear
// within this many steps. Otherwise the motor is probably going the wrong way.
constexpr int32_t LEAVE_MAX_STEPS = 2 * STEPS_PER_REV;

// If the position at an unexpected sensor trip differs from the sensor's known
// position by more than this, the position is re-zeroed to that known position.
// Smaller errors are only logged.
constexpr int32_t ERROR_REZERO_STEPS = 50;

// A user STOP must bring the motor to rest within this time (the ramp from
// LEG_SPEED_MAX_HZ at ACCEL_STEPS_S2 takes about 1 s), or it is a fault.
constexpr unsigned long HALT_TIMEOUT_MS = 3000;

// ---- WiFi / web ----
// Station mode: join the home network and take a DHCP address.
constexpr char HOSTNAME[] = "boom";   // DHCP hostname; also http://boom.local via mDNS
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

// If the home network is not found (e.g. on location), start our own access
// point instead so the tablet can still connect. Set false to disable.
constexpr bool ENABLE_AP_FALLBACK = true;
constexpr char AP_SSID[] = "BoomCtrl";
