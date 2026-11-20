# Outstanding work

Status as of 2026-11-20. Nothing here is verified on hardware unless it says so. Items are grouped by kind; the order inside a group is roughly the order to do them.

## Firmware and testing

- [ ] **Compile sketch 09.** It has only been syntax-checked against stub headers (no Arduino toolchain on the dev machine).
- [ ] **Bench-test sketch 09:** the 800 mA current, the 2000 Hz boundary seek, the eased run speeds, and STOP & DISABLE MOTOR (deceleration, then only RESET accepted).
- [ ] **Sketches 05 to 07 at 740 mA:** they passed on the old TMC2208 at about 400 mA. Only sketch 08 has passed at the current they now use on the Adafruit board.
- [ ] **Confirm the sweep angle.** The README says up to 120 degrees; sketch 09 is configured for 180 (`SWEEP_DEG`).
- [ ] **Calibrate `STEPS_PER_DEGREE`** (142.2 is an estimate from an assumed 16:1 ratio): record the measured travel steps for a known sensor spacing. Verify 3200 steps per motor revolution (200 full steps x 16 microsteps) at the same time.
- [ ] **Loaded test with belt and boom,** with a heatsink on the driver. Start at the slow end, raise `MOTOR_RMS_CURRENT_MA` in steps, time a real 180 degree move. If the fast end loses steps, try SpreadCycle (`en_spreadCycle(true)`, untested).
- [ ] **Hard stops.** Distance still to be decided. Needed before running fast: a sensor stop from run speed is abrupt and the boom's inertia carries it on.
- [ ] **Access point fallback** (used when the home network cannot be joined) is unconfirmed in sketches 03, 07, 08 and 09.

## Safety

- [ ] **Fuse and normally-closed e-stop on the motor supply.** Not wired yet.
- [ ] **Firmware e-stop sensing** (so the firmware knows power was cut) and a re-home afterwards.
- [ ] **Connection-loss watchdog:** a jog or move must stop if the client disappears. A web STOP is not an e-stop.
- [ ] **Web access control** (a PIN or POST-only commands). The endpoints (`/start`, `/stop`, `/home`, `/reset`, `/disable`, `/speed`) are unauthenticated GETs.
- [ ] **Sensor fail-safe.** Both sensors are active-HIGH and fail clear: a lost sensor supply or a fallen-off wire reads "clear".
- [ ] **Chrome on Android shows ERR_TOO_MANY_RETRIES** while Samsung Internet and the desktop work. Unresolved; my unconfirmed guess is the single-connection web server meeting Chrome's speculative connections.

## Hardware and records to check

- [ ] **12 V supply:** read the label (voltage, current, polarity).
- [ ] **Adafruit TMC2209 settings to record:** the pot setting (ignored while UART sets the current) and the MS1/MS2 state (they also set the UART address, which matters before adding a second axis on the same UART).
- [ ] **UART resistor:** the 1 kohm in the TX line was assumed unchanged after the driver switch, not re-confirmed.
- [ ] **Capacitor:** `docs/wiring.md` says 100 uF in the old TMC2208 table, but the diagram and README say 470 uF. Say which was fitted.
- [ ] **Optional:** switch the sketches from the `TMC2208Stepper` class to `TMC2209Stepper`. Not needed for anything that has passed so far.

## Features not built yet

- [ ] Absolute moves in degrees from home (go to angle), with an eased sweep over a set duration (planned as sketch 10).
- [ ] The two physical buttons (go to 0, go to 120).
- [ ] The rest of the web UI: presets, jog, home offset.
- [ ] A second (tilt) axis, possibly.

## Housekeeping

- [ ] Comments in sketches 05 and 06 still say "TMC2208".
- [ ] Uncommitted files to commit: `AGENTS.md`, `README.md`, `docs/`, `sketches/04` to `09`.

## Recently resolved

- **R_SENSE:** 0.05 ohm, confirmed from Adafruit's Eagle schematic (R1 and R2 on BRA and BRB) and the "50" marking on the board. Sketches 04 to 09 use it (2026-11-20).
