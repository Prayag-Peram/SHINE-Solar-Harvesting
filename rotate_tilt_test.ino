// ============================================================================
// Rotate + Tilt stepper/encoder test
// Adafruit Feather ESP32-S2 Reverse TFT
//
//   Rotate: Adafruit TMC2209 + 17HS19-2004-ME1K (quadrature encoder on A0-A3), 1/8 microstepping
//   Tilt:   second Adafruit TMC2209 + stepper, 1/16 microstepping, external I2C encoder
//
// Buttons:  D0 = test rotate   D1 = test tilt   D2 = test both together
//           Any button while a test is running = brake smoothly and stop.
//
// Each test: hold (settle) -> zero encoders (start position = 0, so start centered) ->
//   move to target A -> pause -> move to target B -> pause -> repeat
//   Rotate: 0 -> +90 -> -90 -> +90 ...   (180 degree total range)
//   Tilt:   0 -> +60 -> -60 -> +60 ...   (120 degree total range, measured at the tilt output)
// ============================================================================

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Wire.h>

// ===================== Pins =====================
// Rotate axis (TMC2209 A)
const int PIN_ROT_STEP = 9;
const int PIN_ROT_DIR  = 10;
const int PIN_ROT_EN   = 11;     // active LOW

// Rotate encoder (quadrature; only the + lines are decoded)
const int PIN_EA_P = A3;         // EA+
const int PIN_EA_N = A2;         // EA-  (unused, wired)
const int PIN_EB_P = A1;         // EB+
const int PIN_EB_N = A0;         // EB-  (unused, wired)

// Tilt axis (TMC2209 B)
const int PIN_TILT_STEP = 5;
const int PIN_TILT_DIR  = 6;
const int PIN_TILT_EN   = 12;    // active LOW

// Buttons on the Feather. D0 reads LOW when pressed; D1 and D2 read HIGH when pressed.
const int PIN_BTN_ROT  = 0;      // D0
const int PIN_BTN_TILT = 1;      // D1
const int PIN_BTN_BOTH = 2;      // D2

// I2C uses the Feather's default SDA/SCL pins (Wire.begin()).

// ===================== Rotate settings =====================
const int      ROT_FULL_STEPS       = 200;
const int      ROT_MICROSTEPS       = 8;        // TMC2209 A default (MS1/MS2 low)
const long     ROT_STEPS_PER_REV    = (long)ROT_FULL_STEPS * ROT_MICROSTEPS;
const long     ROT_ENC_CPR         = 4000;      // 1000 PPR x 4 (quadrature)
const int8_t   ROT_ENC_SIGN        = -1;        // flip to +1 if rotate Deg runs opposite to Stp
const bool     ROT_DIR_INVERT      = false;     // flip if the motor turns the wrong way
const float    ROT_TARGET_A_DEG    =  90.0f;    // first target, degrees from the start position
const float    ROT_TARGET_B_DEG    = -90.0f;    // second target; the sweep alternates A <-> B

const uint32_t ROT_STEP_INTERVAL_US = 16000;    // cruise speed (time per step): half the earlier speed
                                                // (use 32000 for one quarter of the earlier speed)
const long     ROT_START_SPEED_SPS  = 15;       // steps/s at the start and end of each move
const long     ROT_ACCEL            = 150;      // steps/s^2

// ===================== Tilt settings =====================
const int      TILT_FULL_STEPS      = 200;      // assumes a 1.8 degree motor; change if different
const int      TILT_MICROSTEPS      = 16;       // MS1/MS2 both tied to 3V3
const long     TILT_STEPS_PER_REV   = (long)TILT_FULL_STEPS * TILT_MICROSTEPS;
const float    TILT_GEAR_RATIO     = 10.65f;    // motor revs per tilt (encoder) rev: 6.2 in / 0.582 in = 10.65.
                                                // Confirm by tooth count, or from the Deg reading at the end of a move.
const bool     TILT_DIR_INVERT     = false;     // flip if the tilt moves the wrong way
const float    TILT_TARGET_A_DEG   =  60.0f;    // first target, degrees at the tilt output from the start position
const float    TILT_TARGET_B_DEG   = -60.0f;    // second target; the sweep alternates A <-> B.
                                                // SET BOTH TO A SAFE RANGE: no limit switches.

const uint32_t TILT_STEP_INTERVAL_US = 900;     // tuned so a 120 degree tilt sweep takes about as long
                                                // as the 180 degree rotate sweep (about 13 s)
const long     TILT_START_SPEED_SPS  = 30;
const long     TILT_ACCEL            = 400;     // lower this first if the tilt slips

// External I2C encoder on the tilt axis (defaults are for an AS5600)
const uint8_t  EXT_ENC_ADDR = 0x36;
const uint8_t  EXT_ENC_REG  = 0x0C;             // raw angle, high byte
const long     EXT_ENC_CPR  = 4096;             // counts per revolution (power of two)
const int8_t   EXT_ENC_SIGN = 1;                // flip to -1 if tilt Deg runs opposite to Stp

// ===================== Test timing and display =====================
const uint32_t SETTLE_MS    = 2000;             // hold after starting, before zeroing
const uint32_t PAUSE_MS     = 1500;             // pause between moves
const float    ERR_LIMIT_DEG = 0.30f;           // Rst turns red above this
const uint32_t DISPLAY_MS   = 150;

// ===================== Derived ramp constants (do not edit) =====================
const long TICK_US       = 50;                  // timer tick (20 kHz)
const long TICKS_PER_SEC = 1000000L / TICK_US;
const long RATE_UNIT     = 10000;               // speed stored as steps/s * RATE_UNIT
const long ACCUM_LIMIT   = RATE_UNIT * TICKS_PER_SEC;

// ===================== Axes =====================
enum { AX_ROT = 0, AX_TILT = 1 };

struct Axis {
  // fixed configuration
  uint8_t pinStep, pinDir, pinEn;
  bool    dirInvert;
  long    accel;           // steps/s^2
  long    accelPerTick;    // speed units added per timer tick
  long    vMax, vMin;      // speed units
  // live state shared with the timer interrupt
  volatile long stepsRemaining;
  volatile long commandedSteps;
  volatile long rate;
  volatile long accum;
  volatile bool dirForward;
};

Axis axes[2];

// Button type is declared up here, before any function, so the Arduino IDE's
// auto-generated function prototypes can see it.
struct Button {
  uint8_t  pin;
  bool     activeHigh;
  bool     lastReading;
  bool     stable;
  uint32_t lastChangeMs;
};

void initAxis(Axis &a, uint8_t pStep, uint8_t pDir, uint8_t pEn, bool dirInvert,
              uint32_t stepIntervalUs, long startSps, long accel) {
  a.pinStep = pStep;
  a.pinDir = pDir;
  a.pinEn = pEn;
  a.dirInvert = dirInvert;
  a.accel = accel;
  a.accelPerTick = accel * RATE_UNIT / TICKS_PER_SEC;
  a.vMax = (long)((int64_t)RATE_UNIT * 1000000 / (int64_t)stepIntervalUs);
  a.vMin = startSps * RATE_UNIT;
  a.stepsRemaining = 0;
  a.commandedSteps = 0;
  a.rate = a.vMin;
  a.accum = 0;
  a.dirForward = true;
}

// ===================== Rotate encoder (quadrature) =====================
volatile int32_t encCount = 0;
volatile uint8_t encState = 0;

static const int8_t QUAD_TABLE[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

void IRAM_ATTR encoderISR() {
  uint8_t curr = (digitalRead(PIN_EA_P) << 1) | digitalRead(PIN_EB_P);
  encCount = encCount + ROT_ENC_SIGN * QUAD_TABLE[(encState << 2) | curr];
  encState = curr;
}

// ===================== Tilt encoder (I2C) =====================
bool     extPresent = false;     // device answered at startup
bool     extOk      = false;     // last read succeeded
uint16_t extLastRaw = 0;
long     extCount   = 0;         // unwrapped counts since zero (signed)

bool readExtEncRaw(uint16_t &raw) {
  Wire.beginTransmission(EXT_ENC_ADDR);
  Wire.write(EXT_ENC_REG);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)EXT_ENC_ADDR, 2) != 2) return false;
  uint16_t hi = Wire.read();
  uint16_t lo = Wire.read();
  raw = ((hi << 8) | lo) & (EXT_ENC_CPR - 1);
  return true;
}

void updateExtEnc() {
  if (!extPresent) return;
  uint16_t raw;
  if (!readExtEncRaw(raw)) {
    extOk = false;
    return;
  }
  extOk = true;
  long delta = (long)raw - (long)extLastRaw;      // unwrap the 0..CPR-1 roll-over
  if (delta >  EXT_ENC_CPR / 2) delta -= EXT_ENC_CPR;
  if (delta < -EXT_ENC_CPR / 2) delta += EXT_ENC_CPR;
  extLastRaw = raw;
  extCount += EXT_ENC_SIGN * delta;
}

// ===================== Step generation with ramp (timer interrupt) =====================
hw_timer_t *stepTimer = nullptr;

void IRAM_ATTR stepISR() {
  for (int i = 0; i < 2; i++) {
    Axis &a = axes[i];
    long rem = a.stepsRemaining;
    if (rem <= 0) continue;

    // Ramp: brake when the steps left are only enough to stop, otherwise speed up
    long rate = a.rate;
    long v = rate / RATE_UNIT;                                  // steps/s
    long decelSteps = (v * v) / (2L * a.accel) + 1;
    if (rem <= decelSteps) {
      rate -= a.accelPerTick;
      if (rate < a.vMin) rate = a.vMin;
    } else if (rate < a.vMax) {
      rate += a.accelPerTick;
      if (rate > a.vMax) rate = a.vMax;
    }
    a.rate = rate;

    // Phase accumulator: one step each time it crosses the limit
    long acc = a.accum + rate;
    if (acc >= ACCUM_LIMIT) {
      acc -= ACCUM_LIMIT;
      digitalWrite(a.pinStep, HIGH);
      delayMicroseconds(2);
      digitalWrite(a.pinStep, LOW);
      a.stepsRemaining = rem - 1;
      a.commandedSteps = a.commandedSteps + (a.dirForward ? 1 : -1);
    }
    a.accum = acc;
  }
}

// ===================== Buttons =====================
Button buttons[3] = {
  { (uint8_t)PIN_BTN_ROT,  false, false, false, 0 },   // D0: LOW when pressed
  { (uint8_t)PIN_BTN_TILT, true,  false, false, 0 },   // D1: HIGH when pressed
  { (uint8_t)PIN_BTN_BOTH, true,  false, false, 0 },   // D2: HIGH when pressed
};

bool readPressed(const Button &b) {
  return digitalRead(b.pin) == (b.activeHigh ? HIGH : LOW);
}

// Returns true once per press (debounced)
bool buttonPressed(Button &b) {
  bool reading = readPressed(b);
  if (reading != b.lastReading) {
    b.lastChangeMs = millis();
    b.lastReading = reading;
  }
  if (millis() - b.lastChangeMs > 40 && reading != b.stable) {
    b.stable = reading;
    if (b.stable) return true;
  }
  return false;
}

// ===================== Test state =====================
enum RunState { STOPPED, SETTLING, RUNNING, STOPPING };
RunState state = STOPPED;

bool     useAxis[2]    = { false, false };   // which axes the current test drives
long     targetStepsA[2] = { 0, 0 };         // target A for each axis, in steps from the start position
long     targetStepsB[2] = { 0, 0 };         // target B for each axis
float    restErrDeg[2] = { 0, 0 };           // error measured at rest after each move
bool     goToA         = true;               // which target the next move heads for
bool     pausing       = false;
uint32_t stateStartMs  = 0;
uint32_t pauseStartMs  = 0;

// ===================== Display =====================
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(240, 135);        // drawn off-screen, then sent in one transfer

// ===================== Position helpers =====================
float axisCmdDeg(int i) {
  if (i == AX_ROT) return axes[AX_ROT].commandedSteps * 360.0f / ROT_STEPS_PER_REV;
  return axes[AX_TILT].commandedSteps * 360.0f / (TILT_STEPS_PER_REV * TILT_GEAR_RATIO);
}

float axisEncDeg(int i) {
  if (i == AX_ROT) return encCount * 360.0f / ROT_ENC_CPR;
  return extCount * 360.0f / EXT_ENC_CPR;
}

// ===================== Motion helpers =====================
void startMove(int i, long steps) {
  Axis &a = axes[i];
  digitalWrite(a.pinDir, (a.dirForward != a.dirInvert) ? HIGH : LOW);
  delayMicroseconds(20);              // DIR setup time
  a.rate = a.vMin;                    // always start from the slow speed
  a.accum = 0;
  a.stepsRemaining = steps;           // set last: this releases the interrupt to step
}

void startActiveMoves() {
  for (int i = 0; i < 2; i++) {
    if (!useAxis[i]) continue;
    long target = goToA ? targetStepsA[i] : targetStepsB[i];
    long delta = target - axes[i].commandedSteps;     // steps still needed to reach the target
    axes[i].dirForward = (delta >= 0);
    startMove(i, labs(delta));
  }
}

bool activeMovesDone() {
  for (int i = 0; i < 2; i++) {
    if (useAxis[i] && axes[i].stepsRemaining > 0) return false;
  }
  return true;
}

void disableAll() {
  for (int i = 0; i < 2; i++) digitalWrite(axes[i].pinEn, HIGH);   // EN is active LOW
}

void zeroCounters() {
  updateExtEnc();                     // fresh baseline for the I2C encoder
  noInterrupts();
  encCount = 0;
  for (int i = 0; i < 2; i++) axes[i].commandedSteps = 0;
  interrupts();
  extCount = 0;
  for (int i = 0; i < 2; i++) restErrDeg[i] = 0;
}

void handlePress(int idx) {          // idx: 0 = rotate, 1 = tilt, 2 = both
  if (state == STOPPED) {
    useAxis[AX_ROT]  = (idx == 0 || idx == 2);
    useAxis[AX_TILT] = (idx == 1 || idx == 2);
    for (int i = 0; i < 2; i++) {
      if (useAxis[i]) digitalWrite(axes[i].pinEn, LOW);            // energize and hold
    }
    state = SETTLING;
    stateStartMs = millis();
  } else if (state == SETTLING) {
    disableAll();
    state = STOPPED;
  } else if (state == RUNNING) {
    pausing = false;
    bool moving = false;
    noInterrupts();
    for (int i = 0; i < 2; i++) {
      if (!useAxis[i] || axes[i].stepsRemaining <= 0) continue;
      // Shorten the move to just the steps needed to brake smoothly
      long v = axes[i].rate / RATE_UNIT;
      long d = (v * v) / (2L * axes[i].accel) + 1;
      if (axes[i].stepsRemaining > d) axes[i].stepsRemaining = d;
      moving = true;
    }
    interrupts();
    if (moving) {
      state = STOPPING;
    } else {
      disableAll();
      state = STOPPED;
    }
  }
  // STOPPING: ignore presses until the motors halt
}

void runStateMachine() {
  if (state == SETTLING) {
    if (millis() - stateStartMs >= SETTLE_MS) {
      zeroCounters();
      goToA = true;
      pausing = false;
      state = RUNNING;
      startActiveMoves();
    }
  } else if (state == RUNNING) {
    if (!pausing && activeMovesDone()) {
      pausing = true;
      pauseStartMs = millis();
    } else if (pausing && millis() - pauseStartMs >= PAUSE_MS) {
      updateExtEnc();
      for (int i = 0; i < 2; i++) {
        if (useAxis[i]) restErrDeg[i] = axisEncDeg(i) - axisCmdDeg(i);   // measured at rest
      }
      pausing = false;
      goToA = !goToA;
      startActiveMoves();
    }
  } else if (state == STOPPING) {
    if (activeMovesDone()) {
      disableAll();
      state = STOPPED;
    }
  }
}

// ===================== Display drawing =====================
void drawColumn(int x, const char *title, int axis) {
  bool haveEnc = (axis == AX_ROT) || extOk;

  canvas.setTextColor((state != STOPPED && useAxis[axis]) ? ST77XX_GREEN : ST77XX_CYAN);
  canvas.setCursor(x, 18);
  canvas.print(title);

  canvas.setTextColor(ST77XX_WHITE);
  canvas.setCursor(x, 36);
  canvas.printf("Stp%6ld", (long)axes[axis].commandedSteps);

  if (haveEnc) {
    float enc = axisEncDeg(axis);
    float err = enc - axisCmdDeg(axis);
    long counts = (axis == AX_ROT) ? (long)encCount : extCount;

    canvas.setCursor(x, 54);
    canvas.printf("Enc%6ld", counts);
    canvas.setCursor(x, 72);
    canvas.printf("Deg%6.1f", enc);
    canvas.setCursor(x, 90);
    canvas.printf("Err%6.2f", err);

    canvas.setTextColor(fabsf(restErrDeg[axis]) <= ERR_LIMIT_DEG ? ST77XX_GREEN : ST77XX_RED);
    canvas.setCursor(x, 108);
    canvas.printf("Rst%6.2f", restErrDeg[axis]);
  } else {
    canvas.setTextColor(ST77XX_RED);
    canvas.setCursor(x, 54);  canvas.print("Enc   ---");
    canvas.setCursor(x, 72);  canvas.print("Deg   ---");
    canvas.setCursor(x, 90);  canvas.print("Err   ---");
    canvas.setCursor(x, 108); canvas.print("Rst   ---");
  }
}

void drawDisplay() {
  canvas.fillScreen(ST77XX_BLACK);
  canvas.setTextSize(2);
  canvas.setTextWrap(false);

  // Top line: button hints when idle, otherwise which test and its state
  canvas.setTextColor(ST77XX_YELLOW);
  canvas.setCursor(0, 0);
  if (state == STOPPED) {
    canvas.print("0:ROT 1:TILT 2:BOTH");
  } else {
    const char *mode = (useAxis[AX_ROT] && useAxis[AX_TILT]) ? "BOTH" :
                       useAxis[AX_ROT] ? "ROT" : "TILT";
    const char *st = (state == SETTLING) ? "SETTLING" :
                     (state == RUNNING)  ? "RUNNING"  : "STOPPING";
    canvas.printf("%s %s", mode, st);
  }

  canvas.drawFastVLine(119, 18, 117, 0x4208);   // divider
  drawColumn(0,   "ROTATE", AX_ROT);
  drawColumn(124, "TILT",   AX_TILT);

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), canvas.width(), canvas.height());
}

// ===================== Setup =====================
void setup() {
  // Drivers start DISABLED (EN is active LOW)
  pinMode(PIN_ROT_STEP, OUTPUT);
  pinMode(PIN_ROT_DIR,  OUTPUT);
  pinMode(PIN_ROT_EN,   OUTPUT);
  pinMode(PIN_TILT_STEP, OUTPUT);
  pinMode(PIN_TILT_DIR,  OUTPUT);
  pinMode(PIN_TILT_EN,   OUTPUT);
  digitalWrite(PIN_ROT_STEP, LOW);
  digitalWrite(PIN_TILT_STEP, LOW);
  digitalWrite(PIN_ROT_EN, HIGH);
  digitalWrite(PIN_TILT_EN, HIGH);

  // Buttons
  pinMode(PIN_BTN_ROT,  INPUT_PULLUP);
  pinMode(PIN_BTN_TILT, INPUT_PULLDOWN);
  pinMode(PIN_BTN_BOTH, INPUT_PULLDOWN);
  delay(10);
  for (int b = 0; b < 3; b++) {                 // ignore whatever state they power up in
    buttons[b].lastReading = buttons[b].stable = readPressed(buttons[b]);
    buttons[b].lastChangeMs = millis();
  }

  // Rotate encoder inputs
  pinMode(PIN_EA_P, INPUT);
  pinMode(PIN_EA_N, INPUT);
  pinMode(PIN_EB_P, INPUT);
  pinMode(PIN_EB_N, INPUT);

  // TFT power + backlight
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);

  tft.init(135, 240);
  tft.setRotation(3);                           // landscape, 240x135
  tft.fillScreen(ST77XX_BLACK);

  // I2C encoder
  Wire.begin();
  Wire.setClock(400000);
  Wire.beginTransmission(EXT_ENC_ADDR);
  extPresent = (Wire.endTransmission() == 0);
  if (extPresent) extOk = readExtEncRaw(extLastRaw);

  // Axes
  initAxis(axes[AX_ROT],  PIN_ROT_STEP,  PIN_ROT_DIR,  PIN_ROT_EN,  ROT_DIR_INVERT,
           ROT_STEP_INTERVAL_US,  ROT_START_SPEED_SPS,  ROT_ACCEL);
  initAxis(axes[AX_TILT], PIN_TILT_STEP, PIN_TILT_DIR, PIN_TILT_EN, TILT_DIR_INVERT,
           TILT_STEP_INTERVAL_US, TILT_START_SPEED_SPS, TILT_ACCEL);
  targetStepsA[AX_ROT]  = lroundf(ROT_TARGET_A_DEG  / 360.0f * ROT_STEPS_PER_REV);
  targetStepsB[AX_ROT]  = lroundf(ROT_TARGET_B_DEG  / 360.0f * ROT_STEPS_PER_REV);
  targetStepsA[AX_TILT] = lroundf(TILT_TARGET_A_DEG / 360.0f * TILT_GEAR_RATIO * TILT_STEPS_PER_REV);
  targetStepsB[AX_TILT] = lroundf(TILT_TARGET_B_DEG / 360.0f * TILT_GEAR_RATIO * TILT_STEPS_PER_REV);

  // Rotate encoder interrupts
  encState = (digitalRead(PIN_EA_P) << 1) | digitalRead(PIN_EB_P);
  attachInterrupt(digitalPinToInterrupt(PIN_EA_P), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_EB_P), encoderISR, CHANGE);

  // Step timer: 1 MHz base, fires every TICK_US (50 us)
  stepTimer = timerBegin(1000000);
  timerAttachInterrupt(stepTimer, &stepISR);
  timerAlarm(stepTimer, TICK_US, true, 0);
}

// ===================== Main loop =====================
void loop() {
  // Read the I2C encoder every few milliseconds
  static uint32_t lastExtMs = 0;
  if (millis() - lastExtMs >= 5) {
    lastExtMs = millis();
    updateExtEnc();
  }

  // Buttons (check all three every pass so debouncing keeps up)
  int pressed = -1;
  for (int b = 0; b < 3; b++) {
    if (buttonPressed(buttons[b])) pressed = b;
  }
  if (pressed >= 0) handlePress(pressed);

  runStateMachine();

  static uint32_t lastDrawMs = 0;
  if (millis() - lastDrawMs >= DISPLAY_MS) {
    lastDrawMs = millis();
    drawDisplay();
  }
}
