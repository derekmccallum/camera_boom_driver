# Wiring

Planning notes. Pin positions on the BTT driver are from the standard StepStick layout and BTT's documentation; confirm against the silkscreen on your own boards. Calculated figures (ratio, steps per degree, current) are estimates.

## ESP32 pin plan

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
| 2B 2A 1A 1B | Motor coils (leave disconnected for the first UART test) |
| MS1, MS2 | Unconnected |

- The V3.0 ships as STEP/DIR or UART. This unit has J2 bridged by a surface-mount link, so it is the UART variant. Do not alter J2.
- `R_SENSE` is assumed 0.11 ohm and is not verified.
- The TMC2208 needs VM present before it answers on UART.
- The TMC2208 has no UART addressing. A second axis needs its own UART or a TMC2209.

## Power

- Motor supply: 12 V brick. Voltage, current and polarity are **not yet confirmed from its label**. Check before wiring.
- Keep the ESP32 on USB during bench tests. Do not also feed the 12 V into the ESP32 board's DC jack. The two supplies share only ground.
- Final build: fuse, then e-stop (cutting the motor supply only, so the ESP32 stays alive), then the capacitor and driver.
- Never connect or disconnect the motor with the supply on.

## Optical endstop (BIQU, TCST2103)

| Sensor pin (use PCB labels) | Connect to |
|---|---|
| V | ESP32 3V3 (not 5V) |
| G | ESP32 GND |
| S | GPIO 34 |

Tested as active-LOW when blocked. One reviewer reported the supplied cable plug order not matching the board's pin labels, so wire by the PCB labels.

## Bring-up order

1. `01_blink_button`
2. `02_opto_endstop`
3. `03_wifi_ap_test`
4. `04_tmc2208_uart_test`: motor disconnected, 12 V on VM, EN held HIGH
5. First motor move, unloaded, low current (to be written)
6. Homing, then absolute moves in degrees
7. Buttons, then the web UI and saved presets
8. Calibrate steps per degree on the real arm
