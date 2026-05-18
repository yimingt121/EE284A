#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID "12345678-1234-1234-1234-1234567890ab"
#define CHAR_UUID    "12345678-1234-1234-1234-1234567890ac"

// ---- Fan control pin (PWM) ----
#define FAN_PIN          25       // GPIO driving fan MOSFET/driver
#define FAN_PWM_FREQ     25000    // 25 kHz (above audible)
#define FAN_PWM_RES      8        // 8-bit: 0-255

// ============================================================
// ====== CONTROL STRATEGY PARAMETERS =========================
// ============================================================
const float V_BATT_MIN     = 3.35f;   // Turn fan OFF below this
const float V_BATT_RESUME  = 3.55f;   // Only resume after recovery (hysteresis)
const float V_BATT_FULL    = 4.10f;   // Above this, run at full duty

const float I_HARVEST_HIGH = 50.0f;   // Strong harvesting -> push duty up
const float I_HARVEST_LOW  = 5.0f;    // Net positive -> hold/slow increase
const float I_DISCHARGE    = -20.0f;  // Net drain -> reduce duty

const int PWM_STEP_UP   = 4;
const int PWM_STEP_DOWN = 12;         // Drop faster than climb (safety)
const int PWM_MIN_ON    = 80;         // Below this, fan stalls -> turn OFF
const int PWM_MAX       = 255;
// ============================================================

Adafruit_INA219 ina219;
BLECharacteristic *characteristic;
bool connected = false;
bool restartAdvertising = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*)    { connected = true;  Serial.println("Client connected"); }
  void onDisconnect(BLEServer*) { connected = false; restartAdvertising = true;
                                  Serial.println("Client disconnected"); }
};

int   fanDuty            = 0;
bool  fanEnabled         = false;
float powerAccum_mW      = 0.0f;
uint32_t sampleCount     = 0;
uint32_t lastReportMs    = 0;
uint32_t lastSampleMs    = 0;

// ============================================================
// Control policy: decide fan duty cycle from V and I.
// ============================================================
void updateFanControl(float V, float I_mA) {
  // ---- 1. HARD VOLTAGE PROTECTION (hysteresis) ----
  if (V < V_BATT_MIN) {
    fanEnabled = false;
    fanDuty = 0;
  } else if (V > V_BATT_RESUME) {
    fanEnabled = true;
  }

  if (!fanEnabled) {
    ledcWrite(FAN_PIN, 0);     // core 3.x: write by pin, not channel
    fanDuty = 0;
    return;
  }

  // ---- 2. POWER-BALANCE ADAPTIVE PWM ----
  if (V > V_BATT_FULL) {
    fanDuty = PWM_MAX;
  } else if (I_mA > I_HARVEST_HIGH) {
    fanDuty += PWM_STEP_UP;
  } else if (I_mA > I_HARVEST_LOW) {
    fanDuty += 1;
  } else if (I_mA < I_DISCHARGE) {
    fanDuty -= PWM_STEP_DOWN;
  }

  if (fanDuty > PWM_MAX) fanDuty = PWM_MAX;
  if (fanDuty < 0)       fanDuty = 0;

  int outDuty = (fanDuty < PWM_MIN_ON) ? 0 : fanDuty;
  ledcWrite(FAN_PIN, outDuty);   // core 3.x: write by pin, not channel
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // ---- Fan PWM init (ESP32 Arduino core 3.x API) ----
  ledcAttach(FAN_PIN, FAN_PWM_FREQ, FAN_PWM_RES);
  ledcWrite(FAN_PIN, 0);

  // ---- INA219 init ----
  if (!ina219.begin()) {
    Serial.println("Failed to find INA219 chip");
    while (1) delay(10);
  }
  ina219.setCalibration_16V_400mA();
  Serial.println("INA219 ready");

  // ---- BLE init ----
  BLEDevice::init("ESP32-Lab4-YT");
  BLEDevice::setPower(ESP_PWR_LVL_P9);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService *service = server->createService(SERVICE_UUID);
  characteristic = service->createCharacteristic(
      CHAR_UUID,
      BLECharacteristic::PROPERTY_NOTIFY
  );
  characteristic->addDescriptor(new BLE2902());
  service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("BLE advertising started");

  lastReportMs = millis();
  lastSampleMs = millis();
}

void loop() {
  if (restartAdvertising) {
    delay(200);
    BLEDevice::startAdvertising();
    Serial.println("Advertising restarted");
    restartAdvertising = false;
  }

  uint32_t now = millis();
  if (now - lastSampleMs < 20) return;
  lastSampleMs = now;

  float shuntVoltage_mV = ina219.getShuntVoltage_mV();
  float busVoltage_V    = ina219.getBusVoltage_V();
  float current_mA      = ina219.getCurrent_mA();
  float loadVoltage_V   = busVoltage_V + (shuntVoltage_mV / 1000.0f);
  float power_mW        = loadVoltage_V * current_mA;

  // ---- APPLY CONTROL POLICY ----
  updateFanControl(loadVoltage_V, current_mA);

  powerAccum_mW += power_mW;
  sampleCount++;

  // ---- Send raw 20 ms data over BLE (when connected) ----
  if (connected) {
    char payload[80];
    snprintf(payload, sizeof(payload),
             "R,V=%.3f,I=%.3f,P=%.3f,D=%d",
             loadVoltage_V, current_mA, power_mW, fanDuty);
    characteristic->setValue((uint8_t*)payload, strlen(payload));
    characteristic->notify();
  }

  // ---- Every 10 s: report average power ----
  if (now - lastReportMs >= 10000) {
    float avgPower = (sampleCount > 0) ? (powerAccum_mW / sampleCount) : 0.0f;
    Serial.printf("[10s AVG] V=%.3f V  I=%.2f mA  Pavg=%.2f mW  duty=%d  en=%d\n",
                  loadVoltage_V, current_mA, avgPower, fanDuty, fanEnabled);

    if (connected) {
      char payload[80];
      snprintf(payload, sizeof(payload),
               "A,V=%.3f,Pavg=%.3f,D=%d", loadVoltage_V, avgPower, fanDuty);
      characteristic->setValue((uint8_t*)payload, strlen(payload));
      characteristic->notify();
    }

    powerAccum_mW = 0.0f;
    sampleCount   = 0;
    lastReportMs  = now;
  }
}
