#include <Wire.h>
#include <Adafruit_BME680.h>
#include <esp_adc_cal.h>

// ─── Pin Definitions ───────────────────────────────────────────
const int ANALOG_SENSOR_PIN = 34;
const int BUTTON_PIN        = 38;
const int LED_PIN           = 14;

// ─── BME680 (I2C) ──────────────────────────────────────────────
Adafruit_BME680 bme;

// ─── ADC Calibration ───────────────────────────────────────────
esp_adc_cal_characteristics_t adcChars;

// ─── System State ──────────────────────────────────────────────
bool systemActive    = false;
bool lastButtonState = HIGH;

// ─── Threshold ─────────────────────────────────────────────────
const float TEMP_DIFF_THRESHOLD = 2.0;


// ───────────────────────────────────────────────────────────────
//  ADC Calibration init
// ───────────────────────────────────────────────────────────────
void calibrateADC() {
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adcChars);
}


// ───────────────────────────────────────────────────────────────
//  TMP36 → °C  (calibrated millivolt read)
// ───────────────────────────────────────────────────────────────
float readTMP36_C() {
  uint32_t raw        = analogRead(ANALOG_SENSOR_PIN);
  uint32_t millivolts = esp_adc_cal_raw_to_voltage(raw, &adcChars);
  float    voltage    = millivolts / 1000.0f;
  return (voltage - 0.5f) * 100.0f;
}


// ───────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // ADC setup
  analogSetAttenuation(ADC_11db);
  calibrateADC();

  pinMode(LED_PIN,    OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);

  // BME680 init — HUZZAH32: SDA = GPIO 23, SCL = GPIO 22
  if (!bme.begin()) {
    Serial.println("ERROR: BME680 not found — check SDA (GPIO 23) / SCL (GPIO 22)!");
    while (1);
  }

  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);

  Serial.println("System ready. Press the button to start.");
}


// ───────────────────────────────────────────────────────────────
void loop() {

  // ── Button toggle ────────────────────────────────────────────
  bool currentButtonState = digitalRead(BUTTON_PIN);
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    delay(50);
    if (digitalRead(BUTTON_PIN) == LOW) {
      systemActive = !systemActive;
      Serial.println(systemActive ? "\n▶ System STARTED" : "\n⏹ System STOPPED");
      if (!systemActive) digitalWrite(LED_PIN, LOW);
    }
  }
  lastButtonState = currentButtonState;

  if (!systemActive) {
    delay(100);
    return;
  }

  // ── Read TMP36 ───────────────────────────────────────────────
  float tmp36 = readTMP36_C();

  // ── Read BME680 ──────────────────────────────────────────────
  if (!bme.performReading()) {
    Serial.println("WARNING: BME680 read failed");
    delay(200);
    return;
  }
  float bmeTemp = bme.temperature;

  // ── Compare & drive LED ──────────────────────────────────────
  float diff = abs(tmp36 - bmeTemp);
  bool  ledOn = (diff > TEMP_DIFF_THRESHOLD);
  digitalWrite(LED_PIN, ledOn ? HIGH : LOW);

  // ── Print temperatures ───────────────────────────────────────
  Serial.println("──────────────────────────────────────────");
  Serial.printf("  TMP36  (analog):  %.2f °C\n", tmp36);
  Serial.printf("  BME680 (digital): %.2f °C\n", bmeTemp);
  Serial.printf("  Difference:       %.2f °C\n", diff);
  Serial.printf("  Threshold:        %.2f °C\n", TEMP_DIFF_THRESHOLD);
  Serial.printf("  LED:              %s\n", ledOn ? "ON ◉" : "off");
  Serial.println("──────────────────────────────────────────");

  delay(500);
}