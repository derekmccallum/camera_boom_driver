# Speed, acceleration and current parameters

All speeds are in **microsteps per second (Hz)** and accelerations in **microsteps per second squared**. The sketches use 16 microsteps on a 200-step motor, so **3200 steps = 1 motor revolution**. Every setting lives in the sketch's `config.h`.

Figures marked *estimate* depend on the **estimated 142.2 steps per degree** (from the designed ~16:1 ratio, 3200 x 16 / 360). That value has not been calibrated on the real arm, so every boom-side figure (degrees, degrees per second, move times) scales with the real ratio. Motor-side figures (rpm, steps) do not.

## Driver settings (all sketches 05 to 09)

| Setting | Value | Notes |
|---|---|---|
| Microsteps | 16 | Set over UART. `MICROSTEPS` in `config.h`. The read-back must match exactly |
| Motor current (RMS) | see the table below | Set over UART (the pot is ignored, `I_scale_analog(false)`). The read-back must be within 10 % of the setting, or the motor is never enabled |
| Chopper mode | StealthChop (`en_spreadCycle(false)`), `pwm_autoscale(true)`, `toff(5)` | Quiet. Not changed for speed (see the risks below) |
| `R_SENSE` | 0.05 ohm (sketches 04 to 09; 04 to 08 changed from 0.11 on 2026-11-20) | **Confirmed** from Adafruit's Eagle schematic (R1 and R2, 0.05 ohm, on BRA and BRB; checked 2026-11-20). The board marking "50" agrees. The real current scales with `(R_SENSE + 0.02)`, so a wrong value gives a proportionally wrong current |
| Driver class | `TMC2208Stepper` | Drives the TMC2209 (user-confirmed, chip version 0x21). Not yet changed to the TMC2209 class |
| Peak current | RMS x 1.41 | 800 mA RMS is about 1.13 A peak |

Motor (HANPOSE 17HS4401S): listings say about 1.5 to 1.7 A per phase, so 800 mA RMS is about half of the lower rating. The Adafruit board is rated to 2 A and has no heatsink.

## Parameters by sketch

| Parameter | 05 (tested) | 06 (tested) | 07 (tested) | 08 (tested) | 09 (untested) |
|---|---|---|---|---|---|
| `MOTOR_RMS_CURRENT_MA` | 740 | 740 | 740 | 740 | **800** |
| Seek / measure speed | n/a | 400 Hz | 400 Hz | 400 Hz | **2000 Hz** |
| Run speed | 800 Hz start, 200 to 3200 Hz by `+` / `-` in 200 Hz steps | 400 Hz (`LEG_SPEED_HZ`) | slider 100 to 1600 Hz, default 400 | slider 100 to 1600 Hz, default 400 | slider about 4571 to 12189 Hz, default 4571 (see below) |
| Acceleration | 1600 (`ACCEL`) | 1600 (`ACCEL_STEPS_S2`) | 1600 | 1600 | speed / `RAMP_TIME_S`, i.e. about 5000 at seek, 11427 to 30472 on runs |
| Pause at each end | 1 s between test moves | 1 s | 1 s | 1 s | none: one leg per START |
| `MAX_SEEK_STEPS` | n/a | 64000 | 64000 | 30000 | 36000 |
| `END_MARGIN_STEPS` | n/a | n/a | n/a | 100 | 100 |

In sketch 06 you raised the leg speed to 4000 Hz on the bearing, unloaded. That value is not recorded in the repo, which still shows 400 Hz.

## Sketch 09 in detail

### Seek speed
`SEEK_SPEED_HZ = 2000` is used to find both sensors during calibration and for the first move to home. Acceleration at that speed is 5000 steps/s2.

### Run speed range (set as move time)
The slider range comes from the time to travel `SWEEP_DEG` (180 degrees, ramps included):

| Setting | Value |
|---|---|
| `SWEEP_DEG` | 180 |
| `SWEEP_TIME_FASTEST_S` | 2.5 s (slider maximum) |
| `SWEEP_TIME_SLOWEST_S` | 6.0 s (slider minimum and the default) |
| `RAMP_TIME_S` | 0.4 s (every move ramps up and down over this time, whatever the speed) |

Derived (the formulas are in `config.h`: speed = `SWEEP_DEG` x steps per degree / (time - ramp)):

| | Slowest (default) | Fastest | Seek |
|---|---|---|---|
| Speed | 4571 Hz | 12189 Hz | 2000 Hz |
| Acceleration (speed / 0.4 s) | 11,427 | 30,472 | 5,000 |
| Motor speed | about 86 rpm | about 229 rpm | 37.5 rpm |
| Boom speed (estimate) | about 32 deg/s | about 86 deg/s | about 14 deg/s |
| Distance to ramp down (estimate) | about 914 steps, 6.4 deg | about 2438 steps, 17.1 deg | about 400 steps, 2.8 deg |

Time for a move of D steps is D / speed + `RAMP_TIME_S`. With the estimated ratio a 120 degree move takes about 4.1 s at the slowest and 1.8 s at the fastest. The page shows the estimated time for the current slider position. If the real ratio differs from 142.2, the real times and angular speeds scale with it.

A STOP, a HOME, a STOP & DISABLE and a speed change all use the same ramp rule (acceleration = speed / `RAMP_TIME_S`).

### Working area and margins
- `END_MARGIN_STEPS = 100` (about 0.7 degree): normal moves end this far inside each sensor, so a sensor only trips on lost steps or after a sensor has been moved.
- `MAX_SEEK_STEPS = 36000`: calibration faults if a sensor is not found within this many steps (about 250 degrees at the estimated ratio). At 2000 Hz that is at most 18 s per leg.
- `MIN_TRAVEL_STEPS = 800`, `LEAVE_MAX_STEPS = 2 x 3200`, `ERROR_REZERO_STEPS = 50`, `HALT_TIMEOUT_MS = 3000` as in sketch 08.

## How to change them

- **Slower or faster runs:** change `SWEEP_TIME_SLOWEST_S` / `SWEEP_TIME_FASTEST_S` (both must be longer than `RAMP_TIME_S`). A longer slowest time lowers the slider minimum, for example 30 s gives about 865 Hz.
- **Gentler or sharper ramps:** change `RAMP_TIME_S`. It applies to every move, including STOP and STOP & DISABLE. A longer ramp is gentler but needs a higher cruise speed for the same move time.
- **Seek speed:** `SEEK_SPEED_HZ`. Each sensor stop is abrupt, so overshoot past the sensor grows with it.
- **Current:** `MOTOR_RMS_CURRENT_MA`. The driver reads it back at boot (look for "Configured: ... OK").

## Risks and open questions

- **Torque:** my rough estimate is that the fastest ramp needs about 12 N.cm at the motor for a 2 kg camera at 0.5 m, against about 10 N.cm at 400 mA real (about 20 N.cm at 800 mA, as torque scales roughly with current) even at standstill, falling with speed. The arm's real inertia is unknown and the boom is currently unloaded. Expect stalls at the fast end when it is loaded. Raise the current in steps (800, then 1000 mA) with a heatsink, and check the driver temperature and the `?` status flags.
- **`R_SENSE`:** 0.05 ohm is confirmed from Adafruit's schematic. The real current is the setting times (R_SENSE assumed + 0.02) / (real R + 0.02). On the Adafruit board, sketch 08 (0.11 in the code) therefore ran at about 1.9 times its 400 mA setting, about 740 mA real. Sketches 04 to 07 passed earlier on the BTT TMC2208 at about 400 mA real, if 0.11 was right for that board (never verified). Sketches 04 to 08 now use 0.05 with a 740 mA setting, which gives the same driver register value (CS 12) that sketch 08 passed with, so 04 to 07 now run at that current on the Adafruit board without having been tested at it. Sketch 09 at 800 mA set with 0.05 is about 800 mA real. If sketch 09 had been left at 0.11 it would have delivered about 1.5 A RMS (about 2.1 A peak) on a board with no heatsink. The 0.05 value itself is settled.
- **StealthChop at speed:** StealthChop gives less torque at high step rates than SpreadCycle. If the motor loses steps at the fast end, switching to SpreadCycle (`en_spreadCycle(true)`) may help. This is untested and not a setting in `config.h`.
- **Power:** 12 V limits the usable speed (back-EMF). I have not confirmed the supply's voltage or current rating from its label.
- **Lost steps:** a stalled move ends short of its target without any error until a sensor trips. An unexpected trip calls `forceStop` at run speed, and the boom's inertia then carries it on. Fit hard stops before running fast. Their distance is still to be decided.
- **Speed above anything tested:** the sketch 09 slowest speed (4571 Hz) is above the fastest speed tested on the boom (1600 Hz in sketches 07 and 08), and above the 4000 Hz run in sketch 06.
