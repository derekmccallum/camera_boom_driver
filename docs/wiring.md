# Wiring

Wiring notes. Sections are marked TESTED where the arrangement has been run on the real hardware, and otherwise are plans. Go by the silkscreen on your own boards, not by the standard StepStick layout: this BTT driver's pin names and the PDN_UART pin position both differ from what the standard layout suggests. Calculated figures (ratio, steps per degree, current) are estimates.

## ESP32 pin plan

STEP, DIR, EN and the UART pins below are TESTED with sketches 04 and 05. The second limit sensor and the buttons are still plans.

| Function | GPIO | Notes |
|---|---|---|
| STEP | 25 | to driver STEP |
| DIR | 26 | to driver DIR |
| EN | 27 | active LOW: HIGH disables the motor |
| TMC2208 UART RX | 16 | direct to PDN_UART |
| TMC2208 UART TX | 17 | through 1k to PDN_UART |
| Limit sensor A | 34 | input-only, no internal pull-up |
| Limit sensor B | 35 | input-only, no internal pull-up |
| Buttons | TBD | |
| Onboard LED | 2 | strapping pin, LED only |

Avoid GPIO 0, 2, 12, 15 (boot strapping) for motor signals, GPIO 6-11 (flash), and ADC2 pins while WiFi is on. GPIO 34-39 are input-only.

## TMC2208 (BIGTREETECH V3.0, UART variant)

| Driver pin | Connect to |
|---|---|
| VM | 12 V positive, via about 3 A fuse (and e-stop later) |
| GND (motor side) | Supply negative |
| 100 uF capacitor | Across VM and GND at the driver (long lead positive) |
| VIO | ESP32 3V3 |
| GND (logic side) | ESP32 GND, common with supply negative |
| EN / STEP / DIR | GPIO 27 / 25 / 26 |
| PDN_UART | GPIO 16 direct, and GPIO 17 via 1k |
| OA1 OA2 OB1 OB2 | Motor coils, see the Motor section below (leave disconnected for the first UART test) |
| MS1, MS2 | Unconnected |

- The V3.0 ships as STEP/DIR or UART. This unit has J2 bridged by a surface-mount link, so it is the UART variant. Do not alter J2.
- `R_SENSE` is assumed 0.11 ohm and is not verified.
- The TMC2208 needs VM present before it answers on UART.
- **PDN_UART is the 4th header pin counting EN as 1** (EN, MS1, MS2, PDN_UART). Confirmed working on this unit. The GPIO 16 wire and the resistor from GPIO 17 must both land on that same pin. The 5th pin is also silkscreened PDN-UART but is NOT continuous with the 4th: leave it unconnected, and go by position, not by the label. Putting RX and TX on different pins gives zero bytes back.
- TESTED. UART test (sketch 04) PASSED: chip version 0x20 (TMC2208), writes counted by IFCNT, read-back OK. The current read-back comes from the same R_SENSE the library used to write it, so it does not verify R_SENSE.
- TESTED. First motor move (sketch 05) works: the driver holds 16 microsteps and 397 mA with no over-temperature, short or open-load flags, and the motor runs smoothly up to 2800 Hz unloaded.
- The TMC2208 has no UART addressing. A second axis needs its own UART or a TMC2209.

## Motor (HANPOSE 17HS4401S, NEMA 17, 4-wire harness)

Seller listings disagree on the ratings: about 1.5 to 1.7 A per phase, 1.5 to 1.8 ohm, 2.8 to 3.8 mH, 40 to 43 N.cm holding torque. Torque scales roughly with current, so the 400 mA used in the bench tests gives only about a quarter of the holding torque. For the loaded build use a higher current (roughly 800 to 1000 mA RMS to start, below the TMC2208's 1.2 A RMS rating), fit the heatsink, and watch driver and motor temperatures.

This driver's motor pins are labelled **OA1, OA2, OB1, OB2** (not 1A/1B/2A/2B). OA1 and OA2 are the two ends of one driver coil, and OB1 and OB2 are the two ends of the other. Both wires of one MOTOR coil must go to the SAME driver coil.

Measured through the harness: each coil is about 2.5 ohm (more than the listed 1.5 to 1.8 ohm, probably meter lead and cable resistance). Colours do NOT follow the common convention on this harness: red/green is one coil and blue/black the other, so always measure.

TESTED wiring:

| Motor wire | Motor coil | Driver pin |
|---|---|---|
| Red | coil 1 | OA1 |
| Green | coil 1 | OA2 |
| Blue | coil 2 | OB1 |
| Black | coil 2 | OB2 |

Result: smooth rotation at 397 mA and 16 microsteps, no driver faults. 3200 steps per revolution is assumed (200 full steps x 16) and one turn per 3200 steps has not been explicitly measured. Wiring red/green/blue/black to OA1/OB1/OA2/OB2 (crossing the coils) gives a stuttering half-turn. Swapping the two wires within a coil only reverses direction. Never connect or disconnect the motor with the supply on.

## Power

- Motor supply: 12 V brick. Voltage, current and polarity are **not yet confirmed from its label**. Check before wiring.
- **Power order: switch the 12 V on BEFORE the ESP32 starts (or press the ESP32 EN/reset button after the 12 V is on).** The sketches configure the driver only at boot. One bench run showed the driver on its defaults (8 microsteps, about 718 mA) with the setup lines reporting no reply, which fits the ESP32 having booted with the 12 V off.
- Keep the ESP32 on USB during bench tests. Do not also feed the 12 V into the ESP32 board's DC jack. The two supplies share only ground.
- Final build: fuse, then e-stop (cutting the motor supply only, so the ESP32 stays alive), then the capacitor and driver.
- Never connect or disconnect the motor with the supply on.

## Optical endstop (BIQU, TCST2103)

| Sensor pin (use PCB labels) | Connect to |
|---|---|
| V | ESP32 3V3 (not 5V) |
| G | ESP32 GND |
| S | GPIO 34 |

TESTED as currently wired at 3.3 V: ACTIVE-HIGH (signal reads LOW with the beam clear and HIGH when the beam is blocked, sensor LED lit when blocked). An earlier note here said active-LOW; that was an inference from sketch 02's LED behaviour and is superseded by the measurement with sketch 06's `?` command. One reviewer reported the supplied cable plug order not matching the board's pin labels, so wire by the PCB labels.

### Second sensor and the shuttle sketch (UNTESTED)

Sketch `06_limit_shuttle` uses two sensors, one at each end of the travel. Wire the second sensor the same way as the first, by the PCB labels:

| Sensor | V | G | S |
|---|---|---|---|
| CW limit (the end reached by turning clockwise, viewed from above) | 3V3 | GND | GPIO 34 |
| CCW limit | 3V3 | GND | GPIO 35 |

Wiring diagram (3V3 and GND each feed both sensors; each S goes to its own GPIO):

```
 ESP32 30-pin board                 SENSOR 1 (CW limit)
 +---------------+
 | 3V3 ----------+--+-------------> V
 |               |  |               G <---------+
 | GPIO 34 <-----+--|---------------S           |
 |               |  |                           |
 |               |  |               SENSOR 2 (CCW limit)
 |               |  +-------------> V
 | GPIO 35 <-----+------------------S
 |               |                  G <---------+
 | GND ----------+------------------------------+
 +---------------+
```

Wiring notes:

- Wire by the labels printed on each sensor's PCB (V, G, S), not by the cable plug order, which one reviewer found did not match the board.
- Power both sensors from 3V3, never 5V: a 5V signal on a GPIO can damage the ESP32. Each sensor draws about 20 mA.
- GPIO 34 and 35 are input-only with no internal pull-up, so each sensor board's own pull-up does the work. An unplugged sensor leaves the pin floating, so it can read anything.
- Keep the sensor leads short and away from the motor wires. If you see false triggers, add 100 nF across S and G at the sensor.
- Mount a hard mechanical stop beyond each sensor, and make each flag long enough that the boom can finish decelerating while the sensor is still blocked. An optical sensor that loses power or a wire that falls off reads "clear", so the firmware cannot rely on the sensors alone.

- MEASURED on the bench with sketch 06's `?` command: with the beams clear both pins read LOW, and a blocked beam makes that sensor's pin read HIGH (active-HIGH, sensor LED lit when blocked). So `CW_BLOCKED_IS_HIGH` and `CCW_BLOCKED_IS_HIGH` in `sketches/06_limit_shuttle/config.h` are both `true`. The user has confirmed this measurement is correct. Re-check with `?` (raw pin levels, beam clear and blocked) if a sensor is swapped or rewired.
- UNTESTED. Which DIR level turns the boom clockwise depends on how the motor is mounted: `CW_IS_DIR_HIGH` in the same file. Watch the first run. If the boom goes the wrong way it reaches the other sensor first and the sketch stops with "wrong limit: check direction or sensor wiring"; flip the constant and reset.
- The sketch auto-starts 5 s after boot (send `s` to abort). It is unloaded-motor only at 400 mA. Keep a hand on the e-stop and a physical stop in place: an optical sensor that loses power or a wire that falls off reads "clear".
- Both sensors blocked at once, or either sensor on a cable fault that makes it read blocked, ends in a fault and a disabled motor.

## Bring-up order

1. `01_blink_button` - done
2. `02_opto_endstop` - done (sketch config now set active-HIGH to match the measured wiring; not re-run since)
3. `03_wifi_ap_test` - done (station mode on the home network; AP fallback untested)
4. `04_tmc2208_uart_test` - done: PASS with motor disconnected, 12 V on VM, EN held HIGH
5. `05_first_motor_move` - done: unloaded motor, 400 mA, smooth
6. `06_limit_shuttle` - written, UNTESTED: seek CW limit, measure travel, shuttle between both sensors
7. Homing on the optical endstop, then absolute moves in degrees (next)
8. Buttons, then the web UI and saved presets
9. Calibrate steps per degree on the real arm
