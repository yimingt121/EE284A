// ============================================================
// Open-Loop RPM Measurement
// Drives a continuous-rotation servo at a fixed pulse width,
// uses an IR break-beam sensor to detect one flag per revolution,
// and reports the measured RPM over Serial.
// ============================================================

// ---------- Servo (actuator) ----------
const int servoPin       = 13;
const int pwmResolution  = 16;    // 16-bit resolution
const int pwmFreq        = 50;    // 50 Hz -> 20 ms period

// Pulse widths (microseconds) from the previous subsection.
// Change PULSE_US to test each of the three values.
const uint32_t minPulse = 1550;
const uint32_t midPulse = 1650;
const uint32_t maxPulse = 1750;

const uint32_t PULSE_US = minPulse;   // <-- set to minPulse / midPulse / maxPulse

// ---------- IR break-beam sensor ----------
const int beamPin = 27;

// ---------- RPM measurement state ----------
volatile uint32_t lastEdgeUs = 0;    // time of previous beam break
volatile uint32_t intervalUs = 0;    // most recent period (us/rev)
volatile bool     newSample  = false;
int lastBeamState = HIGH;            // previous digital reading

// ---------- Helpers ----------
uint32_t dutyFromUs(uint32_t pulseUs) {
  const uint32_t maxDuty = (1UL << pwmResolution) - 1;
  return (pulseUs * maxDuty) / 20000UL;   // 20 ms period at 50 Hz
}

const char* pulseLabel(uint32_t p) {
  if (p == minPulse) return "minPulse";
  if (p == midPulse) return "midPulse";
  if (p == maxPulse) return "maxPulse";
  return "custom";
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Sensor
  pinMode(beamPin, INPUT_PULLUP);
  lastBeamState = digitalRead(beamPin);

  // Servo
  ledcAttach(servoPin, pwmFreq, pwmResolution);
  delay(100);
  ledcWrite(servoPin, dutyFromUs(PULSE_US));

  Serial.print("Pulse width: ");
  Serial.print(PULSE_US);
  Serial.print(" us (");
  Serial.print(pulseLabel(PULSE_US));
  Serial.println(")");
  Serial.println("Measuring RPM...");
}

void loop() {
  // Poll the IR sensor and detect a falling edge (beam just got broken).
  // With INPUT_PULLUP: HIGH = beam clear, LOW = beam blocked by the flag.
  int state = digitalRead(beamPin);

  if (lastBeamState == HIGH && state == LOW) {
    uint32_t now = micros();
    if (lastEdgeUs != 0) {
      intervalUs = now - lastEdgeUs;     // time for one revolution
      newSample = true;
    }
    lastEdgeUs = now;
  }
  lastBeamState = state;

  // Report RPM whenever a fresh interval is available.
  if (newSample) {
    newSample = false;
    // RPM = 60 seconds / period(s) = 60 * 1e6 / period(us)
    float rpm = 60.0f * 1.0e6f / (float)intervalUs;

    Serial.print("Pulse = ");
    Serial.print(PULSE_US);
    Serial.print(" us | Period = ");
    Serial.print(intervalUs);
    Serial.print(" us | RPM = ");
    Serial.println(rpm, 2);
  }

  // Timeout: if no edge for >2 s, motor is likely stopped.
  if (lastEdgeUs != 0 && (micros() - lastEdgeUs) > 2000000UL) {
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 1000) {
      Serial.print("Pulse = ");
      Serial.print(PULSE_US);
      Serial.println(" us | RPM = 0 (no edges detected)");
      lastPrint = millis();
    }
  }
}
