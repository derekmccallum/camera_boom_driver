# Project: camera_boom_driver

## Goal
Motorise a ceiling-mounted camera boom so it swings horizontally around a vertical pole, up to 120 degrees, with ESP32 firmware. Control: two physical buttons (go to 0, go to 120) plus a tablet web UI (go to angle over a set duration, home offset, saved presets, jog, big STOP). A second motor (tilt axis) may be added later, so keep the code axis-generic.

## Mechanical design (decided)
- Boom is balanced; about 50 cm from the pole axis. Camera load roughly 2 kg (a guess).
- 200 mm aluminium lazy-susan ball bearing. Inner ring fixed to the 2020/2040 hanging structure; outer ring rotates with the boom and carries a 6 mm GT2 open belt on its rim, ends anchored on the far side, screw tensioner (no springs: they make the drive elastic).
- NEMA 17 on a fixed plate with a 20T GT2 pulley and two idlers pressing the belt onto the rim.
- Calculated ratio about 16:1, so about 17,000 microsteps per 120 degrees at 16 microsteps. This is an ESTIMATE; steps-per-degree must be calibrated on the real arm and stored as a setting.

## Electronics (decided)
- ESP32 30-pin dev board (CH340, ESP32-D0WD-V3) on a breakout/power board; header jumper set to 3.3 V. Classic ESP32 only (not S3/C3).
- Driver: BIGTREETECH TMC2208 V3.0, UART variant (J2 fitted). Single-wire UART: ESP32 RX GPIO16 direct to PDN_UART, TX GPIO17 through 1k to PDN_UART. PDN_UART is the 4th header pin counting EN as 1 (the 5th is also silkscreened PDN-UART but is not connected to the chip). Assumed R_SENSE 0.11 ohm (not verified). The TMC2208 has no UART addressing, so a second axis needs its own UART or a TMC2209.
- Motor: HANPOSE 17HS4401S (NEMA 17, 40 mm body, 1.8 degree, 5 mm D-shaft). Seller listings disagree on ratings: about 1.5 to 1.7 A per phase, 1.5 to 1.8 ohm, 2.8 to 3.8 mH, 40 to 43 N.cm holding torque. Measured about 2.5 ohm per coil through the harness. Sketch 05's 400 mA is only about a quarter of rated current, so expect roughly a quarter of the holding torque: raise the current for the loaded build (the TMC2208 is rated 1.2 A RMS, so stay below that and fit a heatsink). This driver's motor pins are labelled OA1/OA2/OB1/OB2: OA1+OA2 are one driver coil and OB1+OB2 the other, so both wires of one MOTOR coil must go to the same driver coil. Wired and working: red OA1, green OA2, blue OB1, black OB2. Crossing the coils (red/green on OA1/OB1) gives a stuttering half-turn. Swapping the two wires of one coil only reverses direction.
- Motor supply: 12 V brick (voltage, current and polarity NOT yet confirmed from its label). 100 uF cap across VM/GND at the driver, fuse about 3 A, physical e-stop cutting motor supply only, common ground with the ESP32.
- Pins: STEP 25, DIR 26, EN 27 (active low). Limit sensors GPIO 34 and 35 (input-only). Buttons TBD. Avoid strapping pins 0/2/12/15, flash pins 6-11, and ADC2 while WiFi is on.
- Limit sensors: two BIQU optical endstops (TCST2103), powered at 3.3 V, CW limit on GPIO 34 and CCW limit on GPIO 35 (CW viewed from above). Both are ACTIVE-HIGH as currently wired (pin reads LOW when the beam is clear, HIGH when blocked; sensor LED lit when blocked), measured with sketch 06's `?` command and confirmed by the user. An earlier note that GPIO 34 was active-LOW came from an inference from the sketch 02 LED behaviour and was wrong for the current wiring (the earlier behaviour was never explained, possibly different wiring at the time). Always check raw levels with sketch 06's `?` command if a sensor is swapped or rewired. Polarity is a separate constant per sensor in config.h.

## Software decisions
- Arduino IDE, ESP32 Arduino core (pin the working version), custom firmware. Libraries: FastAccelStepper (step pulses), TMCStepper (UART config).
- WiFi: join the home network as a station (DHCP; the router allocates 10.0.0.100; hostname boom / boom.local), with an own-access-point fallback (BoomCtrl) so the tablet can still connect with no router. Credentials in secrets.h (gitignored). ESP32 is 2.4 GHz only. WebServer for now; move to a WebSocket UI later. Presets/calibration in Preferences. Moves use absolute targets from a homed position (never "move 120 from here").
- Upload: macOS, port /dev/cu.usbserial-8410. Uploads failed at 921600 baud; use 115200.

## Status
Working and tested: blink + button, optical endstop, sketch 03 WiFi station mode (joins the home network, gets 10.0.0.100 from the router). TMC2208 UART test (sketch 04) PASSES. Sketch 05 first motor move: unloaded motor turns smoothly at 397 mA, 16 microsteps, up to 2800 Hz, no driver faults (user-confirmed it works well; exactly one revolution per 3200 steps was assumed, not explicitly measured). Written but UNTESTED: AP fallback path (not confirmed).
Sketch 06 limit shuttle (unloaded, 400 mA, bench, not mounted): auto-starts after a 5 s countdown, seeks the CW sensor, measures travel to the CCW sensor, then runs between the two sensors forever at one constant speed (LEG_SPEED_HZ 400 Hz, with acceleration ramps), logging the position error at each hit. User-confirmed the flow of actions is correct. Each sensor stop is an abrupt forceStop, so overshoot past the sensor grows with speed. Not yet confirmed: that CW_IS_DIR_HIGH (currently true) really turns the boom clockwise viewed from above (the hardware is not mounted yet; logically it looks right), the position-error figures over many legs, and the real travel step count. The "wrong limit" fault is the check for a wrong direction.
Not yet done: absolute moves in degrees, buttons, web UI, loaded test with belt and boom, fuse and e-stop wiring. Still unverified: the 12 V brick label (voltage/current/polarity) and R_SENSE.
Next step: sketch 07: home slowly to the CW optical endstop, back off, treat that as 0 degrees, then absolute moves in degrees with STEPS_PER_DEGREE in config.h; motor stays disabled until homed. Confirm the clockwise direction once the motor is mounted. Set step-rate limits from the real ratio (about 16:1, roughly 4 kHz for a 4 s 120 degree move at 16 microsteps), not sketch 05's 3200 Hz test cap.

## Safety requirements
Boom swings at head height. Hardware e-stop independent of firmware and WiFi. Soft limits plus a physical stop. Finite moves only; jog must stop if the client heartbeat is lost. Home at low speed. Motor stays disabled until homed.

## Working rules
- Code from the planning chat was written without hardware access and is UNTESTED: treat it as a starting point and flag anything that fails to compile.
- One sketch per folder (Arduino requirement). Put all pins and tunables in one config header.
- Never commit WiFi passwords: use secrets.h (gitignored) with a committed secrets.example.h.
- Ask before assuming where the above says "not verified" or "TBD".
