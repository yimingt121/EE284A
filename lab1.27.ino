// ============================================================
// Closed-Loop Speed Control (Target = 60 RPM)
// Continuous-rotation servo + IR break-beam sensor.
// Uses the measured RPM to drive a PI controller that adjusts
// the servo pulse width, clamped to [minPulse, maxPulse].
// ============================================================

// ---------- Servo (actuator) ----------
const int servoPin       = 13;
const int pwmResolution  = 16;    // 16-bit
const int pwmFreq        = 50;    // 50 Hz -> 20 ms period

const uint32_t minPulse = 1550;
const uint32_t midPulse = 1650;   // approximate neutral / starting guess
const uint32_t maxPulse = 1750;

// ---------- IR break-beam sensor ----------
const int beamPin = 27;

// ---------- Control target ----------
const float rpmTarget = 60.0f;

// ---------- Controller gains (tune these) ----------
// Units: pulse-width microseconds per (RPM-error) and per (RPM-error * second)
float Kp = 0.8f;
float Ki = 0.4f;

// ---------- Controller state ----------
float pulseCmd   = (float)midPulse;   // current pulse-width command (us)
float integrator = 0.0f;              // integral of error (RPM * s)

// ---------- RPM measurement state ----------
uint32_t lastEdgeUs = 0;
uint32_t intervalUs = 0;
bool     newSample  = false;
int      lastBeamState = HIGH;

// Debounce: ignore edges closer than this (raise if you see double-triggers)
const uint32_t minEdgeSpacingUs = 3000;   // 3 ms

// ---------- Helpers ----------
uint32_t dutyFromUs(uint32_t pulseUs) {
  const uint32_t maxDuty = (1UL << pwmResolution) - 1;
  return (pulseUs * maxDuty) / 20000UL;
}

float clampf(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

void writePulse(uint32_t pulseUs) {
  ledcWrite(servoPin, dutyFromUs(pulseUs));
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(beamPin, INPUT_PULLUP);
  lastBeamState = digitalRead(beamPin);

  ledcAttach(servoPin, pwmFreq, pwmResolution);
  delay(100);
  writePulse((uint32_t)pulseCmd);       // start at neutral

  Serial.println("Closed-loop control: target = 60 RPM");
  Serial.println("time_ms, rpm, error, pulse_us");
}

void loop() {
  // -------- 1. Detect beam edge (HIGH -> LOW) and compute period --------
  int state = digitalRead(beamPin);
  if (lastBeamState == HIGH && state == LOW) {
    uint32_t now = micros();
    if (lastEdgeUs != 0) {
      uint32_t dt = now - lastEdgeUs;
      if (dt > minEdgeSpacingUs) {     // simple debounce
        intervalUs = dt;
        newSample  = true;
        lastEdgeUs = now;
      }
    } else {
      lastEdgeUs = now;
    }
  }
  lastBeamState = state;

  // -------- 2. If the motor has stalled, force a small bump --------
  // No edges for >1.5 s means rpmMeasured ~ 0; treat as a fresh sample.
  static uint32_t lastControlUs = 0;
  if (lastEdgeUs != 0 && (micros() - lastEdgeUs) > 1500000UL) {
    intervalUs = 1500000UL;            // corresponds to ~40 RPM floor; will yield large positive error
    newSample  = true;
    lastEdgeUs = micros();             // avoid retriggering every loop
  }

  // -------- 3. Run controller on each new RPM sample --------
  if (newSample) {
    newSample = false;

    float rpmMeasured = 60.0e6f / (float)intervalUs;   // 60 * 1e6 / period_us
    float error       = rpmTarget - rpmMeasured;

    // dt between control updates (seconds) for the integrator
    uint32_t nowUs = micros();
    float dt = (lastControlUs == 0) ? 0.0f
                                    : (float)(nowUs - lastControlUs) * 1e-6f;
    lastControlUs = nowUs;

    // Anti-windup: only integrate when command is not already saturated
    bool saturatedHigh = (pulseCmd >= (float)maxPulse) && (error > 0);
    bool saturatedLow  = (pulseCmd <= (float)minPulse) && (error < 0);
    if (!saturatedHigh && !saturatedLow) {
      integrator += error * dt;
    }

    // PI control law: pulse = neutral + Kp*e + Ki*∫e dt
    pulseCmd = (float)midPulse + Kp * error + Ki * integrator;
    pulseCmd = clampf(pulseCmd, (float)minPulse, (float)maxPulse);

    writePulse((uint32_t)pulseCmd);

    // -------- 4. Log --------
    Serial.print(millis());       Serial.print(", ");
    Serial.print(rpmMeasured, 2); Serial.print(", ");
    Serial.print(error, 2);       Serial.print(", ");
    Serial.println((uint32_t)pulseCmd);
  }
}
