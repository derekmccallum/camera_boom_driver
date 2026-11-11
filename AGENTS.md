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
- Driver: BIGTREETECH TMC2208 V3.0, UART variant (J2 fitted). Single-wire UART: ESP32 RX GPIO16 direct to PDN_UART, TX GPIO17 through 1k to PDN_UART. Assumed R_SENSE 0.11 ohm (not verified). The TMC2208 has no UART addressing, so a second axis needs its own UART or a TMC2209.
- Motor supply: 12 V brick (voltage, current and polarity NOT yet confirmed from its label). 100 uF cap across VM/GND at the driver, fuse about 3 A, physical e-stop cutting motor supply only, common ground with the ESP32.
- Pins: STEP 25, DIR 26, EN 27 (active low). Limit sensors GPIO 34 and 35 (input-only). Buttons TBD. Avoid strapping pins 0/2/12/15, flash pins 6-11, and ADC2 while WiFi is on.
- Limit sensor: BIQU optical endstop (TCST2103), powered at 3.3 V, TESTED as active-LOW when blocked.

## Software decisions
- Arduino IDE, ESP32 Arduino core (pin the working version), custom firmware. Libraries: FastAccelStepper (step pulses), TMCStepper (UART config).
- WiFi: join the home network as a station (DHCP; the router allocates 10.0.0.100; hostname boom / boom.local), with an own-access-point fallback (BoomCtrl) so the tablet can still connect with no router. Credentials in secrets.h (gitignored). ESP32 is 2.4 GHz only. WebServer for now; move to a WebSocket UI later. Presets/calibration in Preferences. Moves use absolute targets from a homed position (never "move 120 from here").
- Upload: macOS, port /dev/cu.usbserial-8410. Uploads failed at 921600 baud; use 115200.

## Status
Working and tested: blink + button, optical endstop, sketch 03 WiFi station mode (joins the home network, gets 10.0.0.100 from the router). Written but UNTESTED: AP fallback path (not confirmed), TMC2208 UART test (sketch 04).
Not yet done: TMC2208 UART test, first motor move, homing, UI.
Next step: sketches/04_tmc2208_uart_test with motor disconnected and 12 V on VM only.

## Safety requirements
Boom swings at head height. Hardware e-stop independent of firmware and WiFi. Soft limits plus a physical stop. Finite moves only; jog must stop if the client heartbeat is lost. Home at low speed. Motor stays disabled until homed.

## Working rules
- Code from the planning chat was written without hardware access and is UNTESTED: treat it as a starting point and flag anything that fails to compile.
- One sketch per folder (Arduino requirement). Put all pins and tunables in one config header.
- Never commit WiFi passwords: use secrets.h (gitignored) with a committed secrets.example.h.
- Ask before assuming where the above says "not verified" or "TBD".
