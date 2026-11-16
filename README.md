# Rotate + Tilt Stepper/Encoder Test

Arduino sketch (`rotate_tilt_test.ino`) for a two-axis rotate/tilt bench setup on an **Adafruit Feather ESP32-S2 Reverse TFT**.

- **Rotate axis:** 17HS19-2004-ME1K stepper (built-in 1000 PPR magnetic encoder) on an Adafruit TMC2209, 1/8 microstepping.
- **Tilt axis:** a second stepper on a second Adafruit TMC2209, 1/16 microstepping, driving the tilt through a gear reduction, with an **AS5600** magnetic encoder over I2C reading the tilt output.

Three buttons on the Feather start tests for rotate, tilt, or both together. Each test sweeps back and forth while the sketch reads the encoders and shows the commanded position, measured position, and error for both axes on the built-in TFT.

It is a bench test for checking that motors, drivers, and encoders agree with each other. It is open-loop: the encoders are measured and displayed, but they do not correct the motors.

---

## Hardware

| Part | Notes |
|---|---|
| Adafruit Feather ESP32-S2 Reverse TFT | Built-in 240x135 TFT and three buttons (D0, D1, D2) |
| Adafruit TMC2209 breakout (A, rotate) | STEP/DIR mode, default 1/8 microstepping |
| Adafruit TMC2209 breakout (B, tilt) | STEP/DIR mode, MS1 and MS2 tied to 3V3 for 1/16 microstepping |
| 17HS19-2004-ME1K (rotate motor) | 200 full steps/rev, 2.0 A rated, quadrature encoder 1000 PPR (4000 counts/rev) |
| Tilt stepper | Assumed 200 full steps/rev; geared down to the tilt output |
| Tilt gear reduction | Estimated 10.65:1 (6.2 in / 0.582 in); to be confirmed |
| AS5600 magnetic encoder | I2C address 0x36, 12-bit (4096 counts/rev), reads the tilt output |
| 12 V supply | Powers both TMC2209 motor supply (VM) inputs |
| 12 V to 5 V adapter | Feeds 5 V into the Feather's USB pin |

## Wiring

| Signal | Feather label | Header pin | Connects to |
|---|---|---|---|
| Rotate encoder EB- | A0 | 5 | Encoder FBM / EB- (wired, not decoded) |
| Rotate encoder EB+ | A1 | 6 | Encoder FBP / EB+ |
| Rotate encoder EA- | A2 | 7 | Encoder FAM / EA- (wired, not decoded) |
| Rotate encoder EA+ | A3 | 8 | Encoder FAP / EA+ |
| Rotate STEP | GPIO9 | 21 | TMC2209 A STEP |
| Rotate DIR | GPIO10 | 22 | TMC2209 A DIR |
| Rotate EN | GPIO11 | 23 | TMC2209 A EN (active LOW) |
| Tilt STEP | GPIO5 | 19 | TMC2209 B STEP |
| Tilt DIR | GPIO6 | 20 | TMC2209 B DIR |
| Tilt EN | GPIO12 | 24 | TMC2209 B EN (active LOW) |
| I2C SDA | SDA | 17 | AS5600 SDA |
| I2C SCL | SCL | 18 | AS5600 SCL |
| 5 V in | USB pin | 26 | 12 V to 5 V adapter output |
| 3.3 V | 3V | 2, 3 | TMC2209 VIO (both drivers), AS5600 supply |

Buttons are built into the Feather: **D0** (GPIO0) reads LOW when pressed; **D1** (GPIO1) and **D2** (GPIO2) read HIGH when pressed.

Also confirm on your build:

- 12 V supply to each TMC2209's **VM** and **GND**.
- All grounds common (supply, both drivers, Feather, encoders).
- Rotate encoder power and ground per the motor datasheet.
- Motor coils on the correct driver output pairs.
- I2C pull-ups on SDA and SCL (many AS5600 breakouts include them).

### Power cautions

- Do not plug in USB while the 5 V adapter is feeding the USB pin. Disconnect the adapter before uploading code.
- The ESP32-S2 inputs are not 5 V tolerant. Check the rotate encoder's output voltage in the motor datasheet; use a level shifter or differential receiver if it swings to 5 V.
- Set each TMC2209's current with its on-board potentiometer. Do not exceed the motor rating (2 A per phase on the rotate motor). Add a heatsink (and a fan for long runs); the driver chips heat quickly at higher currents.

---

## Software setup

1. Install the Arduino IDE.
2. In **Boards Manager**, install **esp32 by Espressif Systems**, version **3.0 or later** (the sketch uses the 3.x hardware timer API).
3. In **Library Manager**, install **Adafruit GFX Library**, **Adafruit ST7735 and ST7789 Library**, and their dependency **Adafruit BusIO**. `Wire` and `SPI` are built in.
4. Select **Tools > Board > Adafruit Feather ESP32-S2 Reverse TFT**.
5. Keep `rotate_tilt_test.ino` in a folder named `rotate_tilt_test` (the Arduino IDE requires the folder to match the sketch name).
6. Upload over USB with the 5 V adapter disconnected, then run from the adapter if desired.

---

## Using it

At power-up the drivers are disabled and the top line of the screen reads `0:ROT 1:TILT 2:BOTH`.

| Button | Test |
|---|---|
| **D0** | Rotate |
| **D1** | Tilt |
| **D2** | Rotate and tilt together |

**Before pressing a button, put both axes at their center position.** The sketch treats wherever the axes are at the start of a test as 0.

A test runs like this:

1. **Settling:** the drivers energize and hold for 2 seconds. Both drivers are enabled for every test, so the axis that isn't being tested holds its position instead of coasting.
2. **Zeroing:** the encoders and step counters are set to 0.
3. **Sweeping:** each axis alternates between two targets, with a 1.5 second pause after each move.

| Axis | Sweep (from the start position) | Total range |
|---|---|---|
| Rotate | 0 to +90, then -90, then +90, and so on | 180 degrees |
| Tilt | 0 to +60, then -60, then +60, and so on | 120 degrees (at the tilt output) |

In the combined test both axes start each move together and wait for the slower one before pausing and reversing.

**Pressing any button while a test is running** brakes the motors smoothly, then releases both drivers. Pressing a button during the settling hold cancels the test. Button presses are ignored while braking.

### Typical move times

| Axis | First move | Each sweep |
|---|---|---|
| Rotate (90 / 180 degrees) | about 6.7 s | about 13.1 s |
| Tilt (60 / 120 degrees) | about 7.7 s | about 12.8 s |

The tilt speed is set so its sweep takes about as long as the rotate sweep.

### Display

The top line shows the button hints when idle, or the test and state while running (`ROT`, `TILT`, or `BOTH`, plus `SETTLING`, `RUNNING`, or `STOPPING`). Below it are two columns, ROTATE on the left and TILT on the right.

| Row | Meaning |
|---|---|
| Title | Green while that axis is part of the running test, cyan otherwise |
| `Stp` | Microsteps commanded since the test started (signed) |
| `Enc` | Encoder counts since the test started (signed) |
| `Deg` | Encoder position in degrees |
| `Err` | Live error in degrees (measured minus commanded) |
| `Rst` | Error in degrees measured at rest after the last move; green within 0.30 degrees, red above |

If the tilt encoder is not found or stops answering, the tilt `Enc`, `Deg`, `Err`, and `Rst` rows show `---`.

---

## How it works

### Code layout (top to bottom)

1. **Pins and settings** - all tunable constants, grouped by rotate, tilt, I2C encoder, and test timing.
2. **Derived ramp constants** - computed from the settings; do not edit.
3. **Axis and button types** - declared before any function so the Arduino IDE's auto-generated prototypes compile.
4. **Rotate encoder** - interrupt that decodes the quadrature signal.
5. **Tilt encoder** - I2C read and roll-over unwrapping for the AS5600.
6. **Step generation** - one timer interrupt that steps both motors and runs the acceleration ramp.
7. **Buttons, test state, display objects, position helpers.**
8. **Motion helpers and state machine** - starting moves, stopping, the test sequence.
9. **Display drawing, `setup()`, and `loop()`.**

### Rotate encoder

The quadrature signal is decoded from the plus lines only (EA+ on `A3`, EB+ on `A1`). A pin-change interrupt on either channel looks up the previous and current A/B state in a 16-entry table that returns +1, -1, or 0 and adds it to `encCount`. This is 4x decoding: 4000 counts per revolution, 0.09 degrees per count. `ROT_ENC_SIGN` flips the count direction. The minus lines (`A0`, `A2`) are configured as inputs but are not used.

### Tilt encoder (AS5600)

Every 5 ms the sketch reads the AS5600's raw angle register (0x0C) over I2C at 400 kHz, giving 12 bits (4096 counts per revolution, about 0.088 degrees per count). Because the raw angle wraps from 4095 to 0, each read is compared with the previous one and the jump is unwrapped into a running signed count, so the tilt can sweep through any range without losing track. `EXT_ENC_SIGN` flips the direction.

### Step generation and the ramp

A hardware timer interrupts every **50 microseconds** (20 kHz) and handles both axes. Each axis uses a phase accumulator: every tick adds the axis's current speed to the accumulator, and each time it crosses a threshold the interrupt emits a 2 microsecond pulse on STEP and counts the step.

The same interrupt runs the acceleration ramp, in integer math:

- Speed increases by the axis's `ACCEL` until it reaches the cruise speed set by its `STEP_INTERVAL_US`.
- When the steps remaining equal the steps needed to stop from the current speed (`speed^2 / (2 * ACCEL) + 1`), it slows down instead.
- Speed never drops below the axis's start speed, so every move starts and ends slowly.

Because stepping lives in the interrupt, display updates (which take tens of milliseconds) never disturb the motors.

### Moves are to targets

Each axis has two targets, A and B, stored as step counts from the start position. For each move, the sketch compares the target with the axis's current commanded position, sets the direction, and steps the difference. The first move goes to A, the next to B, and so on. Tilt targets are measured at the tilt output: the number of motor steps is the target angle times the gear ratio times the steps per motor revolution.

### State machine

`STOPPED` -> `SETTLING` (drivers energized, hold) -> `RUNNING` (move, pause, record rest error, reverse) -> `STOPPING` (braking after a stop request) -> `STOPPED`. A stop request shortens the move in progress to just the braking distance, so the motor decelerates on the ramp rather than halting abruptly.

### Display

The screen is drawn into an off-screen 240x135 canvas and sent to the TFT in one transfer every 150 ms, which avoids flicker.

---

## Settings reference

All at the top of the sketch.

### Rotate

| Constant | Default | Meaning |
|---|---|---|
| `ROT_FULL_STEPS` | 200 | Full steps per revolution |
| `ROT_MICROSTEPS` | 8 | Must match the TMC2209 A microstep setting |
| `ROT_ENC_CPR` | 4000 | Encoder counts per revolution (1000 PPR x 4) |
| `ROT_ENC_SIGN` | -1 | Set to +1 if rotate `Deg` runs opposite to `Stp` |
| `ROT_DIR_INVERT` | false | Reverses the rotate motor direction |
| `ROT_TARGET_A_DEG` / `ROT_TARGET_B_DEG` | +90 / -90 | Sweep targets, degrees from the start position |
| `ROT_STEP_INTERVAL_US` | 16000 | Cruise speed (time per step); about 62 steps/s |
| `ROT_START_SPEED_SPS` | 15 | Speed at the start and end of each move (steps/s) |
| `ROT_ACCEL` | 150 | Ramp acceleration (steps/s^2) |

### Tilt

| Constant | Default | Meaning |
|---|---|---|
| `TILT_FULL_STEPS` | 200 | Full steps per motor revolution (assumed) |
| `TILT_MICROSTEPS` | 16 | Must match the TMC2209 B setting (MS1/MS2 high) |
| `TILT_GEAR_RATIO` | 10.65 | Motor revolutions per tilt revolution |
| `TILT_DIR_INVERT` | false | Reverses the tilt motor direction |
| `TILT_TARGET_A_DEG` / `TILT_TARGET_B_DEG` | +60 / -60 | Sweep targets, degrees at the tilt output |
| `TILT_STEP_INTERVAL_US` | 900 | Cruise speed; about 1100 steps/s |
| `TILT_START_SPEED_SPS` | 30 | Speed at the start and end of each move (steps/s) |
| `TILT_ACCEL` | 400 | Ramp acceleration (steps/s^2); lower this first if the tilt slips |

### I2C encoder (AS5600)

| Constant | Default | Meaning |
|---|---|---|
| `EXT_ENC_ADDR` | 0x36 | I2C address |
| `EXT_ENC_REG` | 0x0C | Raw angle register (high byte) |
| `EXT_ENC_CPR` | 4096 | Counts per revolution (power of two) |
| `EXT_ENC_SIGN` | +1 | Set to -1 if tilt `Deg` runs opposite to `Stp` |

### Test timing and display

| Constant | Default | Meaning |
|---|---|---|
| `SETTLE_MS` | 2000 | Hold after starting, before zeroing |
| `PAUSE_MS` | 1500 | Pause between moves |
| `ERR_LIMIT_DEG` | 0.30 | `Rst` turns red above this many degrees |
| `DISPLAY_MS` | 150 | Screen refresh interval |

Speeds and ramps are expressed in steps, so they depend on the microstepping. The tilt runs at 1/16, so it needs twice the step rate of a 1/8 axis for the same motor speed.

---

## Understanding the numbers

`Err` is the measured angle minus the commanded angle, in degrees. Useful scales:

| Quantity | Value |
|---|---|
| Rotate encoder, 1 count | 0.09 degrees |
| AS5600, 1 count | about 0.088 degrees |
| Rotate, 1 microstep (1/8) | 0.225 degrees |
| Tilt, 1 microstep at the output (1/16, 10.65:1) | about 0.011 degrees |
| Rotate full step | 1.8 degrees |

- **`Err` during motion** is following error: the rotor trails the commanded position a little while moving, more at higher speed, acceleration, or load.
- **`Rst`** is measured after each move, once the motor has settled and just before the next move starts. It is the number to watch against the 0.30 degree goal.
- **Lost steps** show up as `Rst` that jumps or keeps growing from move to move. A small `Rst` that stays steady means no steps are being lost.
- A **systematic** tilt error that scales with distance moved points to a wrong gear ratio or encoder resolution, not lost steps.

---

## Current status and known issues

- Rotate and tilt tests, the combined test, button start/stop, and the display are all working.
- **Tilt error is larger than the 0.30 degree goal.** The cause is believed to be the AS5600 magnet mounting (alignment, centering, and air gap) and the exact gear ratio, which is still an estimate. Both are being fixed mechanically, not in code.

### Finalizing the gear ratio

Once the magnet mounting is fixed, confirm the ratio by counting teeth on the two gears (ratio = large gear teeth / small gear teeth). To check it from the machine instead:

1. Run the tilt test and read `Deg` at the end of a forward move (for example, when it should read +60).
2. The true ratio is the current `TILT_GEAR_RATIO` multiplied by the commanded angle and divided by the measured angle.
3. Enter the result in `TILT_GEAR_RATIO`.

For example, with a ratio of 10.65 and a commanded 60 degrees that measures 56.3 degrees, the true ratio is 10.65 x 60 / 56.3, about 11.35. This only works once the AS5600 is mounted reliably and its counts per revolution are correct.

---

## Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| Compile error: `'Button' does not name a type` | The `Button` type must be declared before the first function. It is, in the current file; make sure you are using the latest copy |
| Compile error on `timerBegin` | ESP32 core is below 3.0; update it |
| Screen shows only the idle line, no values change | The test is not running; press D0, D1, or D2 |
| A button starts a test on release, or never triggers | Button polarity differs from the assumption in `buttons[]`; flip that button's `activeHigh` |
| Tilt rows show `---` | AS5600 not found or not answering: check SDA/SCL, 3.3 V, pull-ups, and address 0x36 |
| `Deg` runs opposite to `Stp` | Flip `ROT_ENC_SIGN` (rotate) or `EXT_ENC_SIGN` (tilt) |
| Axis moves the wrong direction physically | Flip `ROT_DIR_INVERT` or `TILT_DIR_INVERT` |
| Axis slips at start or stop | Lower that axis's `START_SPEED_SPS` and `ACCEL`, or raise its `STEP_INTERVAL_US` |
| Tilt `Rst` is large and grows with distance | Gear ratio is off (see above) |
| Tilt `Deg` jumps or drifts | AS5600 magnet misaligned, off-center, or wrong air gap |
| Driver gets too hot or the motor weakens and stalls | Verify the current setting, add a heatsink and fan, shorten `SETTLE_MS` and `PAUSE_MS`. Both drivers are energized during every test, so the idle axis also draws holding current |
| Board seems stuck after a reset | Do not hold D0 while pressing reset (D0 is the bootloader button) |

---

## Limitations and ideas

- **Open loop.** Staying under 0.30 degrees at all times, including during motion, needs closed-loop control (firmware using the encoders to correct commanded steps, or closed-loop drivers).
- **No limit switches or homing.** The start position is treated as center. Keep both sweeps inside the mechanism's safe range.
- **Motor torque is capped by the motor.** A driver with higher current capability will not add torque beyond the motor's rating; use more gearing or a larger motor for more.
- **Possible additions:** an end-of-move correction that nudges each axis until its encoder is within tolerance; showing the AS5600's magnet-detected, too-weak, and too-strong status on screen to help with magnet alignment; limit switches and a homing routine.
