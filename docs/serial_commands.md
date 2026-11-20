# Serial monitor commands

All sketches print at **115200 baud**. In the Arduino IDE serial monitor, type a single character and press Enter (the line ending can be anything). Commands are single characters and case-sensitive. Any unknown character prints the help list again.

Power the 12 V first, then start or reset the ESP32: the sketches configure the driver only at boot.

| Sketch | Commands |
|---|---|
| 01 to 03 | None. Output only (03 prints the IP address it received). |
| 04 | Any character re-runs the UART test. |
| 05 | See the table below. |
| 06 | `s`, `d`, `?` |
| 07 | `g`, `s`, `d`, `?` |
| 08 | `g`, `s`, `h`, `r`, `d`, `?` |
| 09 | `g`, `s`, `h`, `r`, `d`, `?` |

## Sketch 05: first motor move (unloaded)

| Key | Action |
|---|---|
| `e` | Enable the motor (it holds position, so the shaft feels stiff) |
| `d` | Disable the motor |
| `f` / `r` | Move forward / reverse 1 revolution (3200 steps at 16 microsteps) |
| `F` / `R` | Move forward / reverse 1/8 revolution |
| `t` | Auto test: forward 1 revolution, pause, back 1 revolution |
| `s` | STOP: decelerates, the motor stays enabled |
| `+` / `-` | Speed up / down in steps of 200 Hz (200 to 3200 Hz, starts at 800 Hz) |
| `z` | Zero the position counter |
| `?` | Status and driver diagnostics |

The motor is disabled again automatically after 15 s idle.

## Sketch 06: limit shuttle

The sketch starts by itself 5 s after boot, so keep the boom clear and a hand on the power switch.

| Key | Action |
|---|---|
| `s` | STOP immediately and disable the motor. The sequence does not resume: reset the board to restart |
| `d` | Disable the motor (same effect as `s`) |
| `?` | State, position, sensors and driver diagnostics |

## Sketch 07: web shuttle

It waits for START and does not auto-start.

| Key | Action |
|---|---|
| `g` | Start, or resume after a STOP (same as the START button) |
| `s` | STOP: decelerate and hold, motor stays enabled (same as the STOP button) |
| `d` | Hard stop and disable the motor. Reset the board to restart |
| `?` | State, position, sensors and driver diagnostics |

## Sketches 08 and 09: self-calibrating web boom

Both keep the same keys. They calibrate by themselves a few seconds after boot (the boom moves; `s` or STOP cancels the countdown).

| Key | Action |
|---|---|
| `g` | START (same as the START button). Ignored until the boundaries are known. In 09 it sends the boom one leg in the current direction |
| `s` | STOP: decelerate and hold, motor stays enabled (same as the STOP button) |
| `h` | HOME: go to the home position (same as the HOME button). Ignored until calibrated |
| `r` | RESET: seek both sensors again (same as the RESET button) |
| `d` | Hard stop and disable the motor. The board then ignores everything until it is reset |
| `?` | State, position, sensors and driver diagnostics |

**Sketch 09 only: STOP & DISABLE MOTOR.** The web page also has a STOP & DISABLE MOTOR button, which has no serial key. It decelerates the motor on the normal ramp and then disables it so the boom can be moved by hand (pressing it again while it is decelerating disables at once), and afterwards only RESET (the page button or `r`) is accepted. This differs from `d`, which leaves the sketch stopped until the board is reset. This feature is untested.

## What `?` prints

- The state, the position in steps, the measured travel (07 to 09), and whether the motor is moving.
- Both limit sensors: the raw pin level, whether that means blocked or clear, and the debounced state. Use it to check sensor polarity after any rewiring (both sensors read HIGH when blocked).
- The driver: the configured current and microsteps, `cs_actual`, over-temperature and pre-warning flags, short-to-ground and short-to-supply flags, open-load flags (only meaningful while stationary and enabled), and the GSTAT flags. If the driver does not answer, it says so and points at the 12 V supply and wiring.
- The driver code is written for the TMC2208 class. The Adafruit TMC2209 answers with chip version 0x21, which appears in the boot output.

## Boot messages worth reading

- `chip version 0x21` and `Configured: … OK` mean the driver answered and accepted its settings. A mismatch means the motor will not be enabled.
- The seek, speed, ramp and margin settings.
- The raw state of both sensors at boot.
- The IP address, and `http://boom.local` where mDNS works.
