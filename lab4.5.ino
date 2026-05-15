#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID "12345678-1234-1234-1234-1234567890ab"
#define CHAR_UUID    "12345678-1234-1234-1234-1234567890ac"

const int FAN_PIN  = A0;
const int PWM_FREQ = 25000;
const int PWM_RES  = 8;

struct Phase { uint8_t duty; const char* label; uint32_t durationMs; };
Phase phases[] = {
  {  0, "OFF",  10000 },
  { 64, "25",   60000 },
  {  0, "OFF",   5000 },
  {128, "50",   60000 },
  {  0, "OFF",   5000 },
  {255, "100",  60000 },
  {  0, "DONE",     0 }
};
int currentPhase = 0;
uint32_t phaseStart = 0;
bool sequenceStarted = false;     // <-- new

Adafruit_INA219 ina219;
BLECharacteristic *characteristic;
bool connected = false;
bool restartAdvertising = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*)    { connected = true;  Serial.println("Client connected"); }
  void onDisconnect(BLEServer*) { connected = false; restartAdvertising = true;
                                  sequenceStarted = false;       // <-- reset
                                  currentPhase = 0;               // <-- reset
                                  Serial.println("Client disconnected"); }
};

void applyPhase(int idx) {
  ledcWrite(FAN_PIN, phases[idx].duty);
  Serial.print("PHASE:"); Serial.println(phases[idx].label);

  // Send marker multiple times to make sure the client catches it
  for (int i = 0; i < 3; i++) {
    if (connected) {
      char marker[32];
      snprintf(marker, sizeof(marker), "PHASE=%s", phases[idx].label);
      characteristic->setValue((uint8_t*)marker, strlen(marker));
      characteristic->notify();
      delay(50);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  ledcAttach(FAN_PIN, PWM_FREQ, PWM_RES);
  ledcWrite(FAN_PIN, 0);

  if (!ina219.begin()) {
    Serial.println("Failed to find INA219 chip");
    while (1) delay(10);
  }
  ina219.setCalibration_16V_400mA();
  Serial.println("INA219 ready");

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
  Serial.println("BLE advertising started, waiting for client...");
}

void loop() {
  if (restartAdvertising) {
    delay(200);
    BLEDevice::startAdvertising();
    Serial.println("Advertising restarted");
    restartAdvertising = false;
  }

  // Wait for connection before starting the phase sequence
  if (!sequenceStarted && connected) {
    delay(2000);  // let client finish subscribing to notifications
    phaseStart = millis();
    applyPhase(currentPhase);
    sequenceStarted = true;
  }

  if (sequenceStarted &&
      phases[currentPhase].durationMs > 0 &&
      millis() - phaseStart >= phases[currentPhase].durationMs) {
    currentPhase++;
    phaseStart = millis();
    applyPhase(currentPhase);
  }

  float shuntVoltage_mV = ina219.getShuntVoltage_mV();
  float busVoltage_V    = ina219.getBusVoltage_V();
  float current_mA      = ina219.getCurrent_mA();
  float loadVoltage_V   = busVoltage_V + (shuntVoltage_mV / 1000.0);

  Serial.print("Bus: ");      Serial.print(busVoltage_V, 3);    Serial.print(" V  ");
  Serial.print("Shunt: ");    Serial.print(shuntVoltage_mV, 3); Serial.print(" mV  ");
  Serial.print("Load (batt): "); Serial.print(loadVoltage_V, 3); Serial.print(" V  ");
  Serial.print("I: ");        Serial.print(current_mA, 3);      Serial.println(" mA");

  if (connected) {
    char payload[64];
    snprintf(payload, sizeof(payload), "V=%.3f,I=%.3f", loadVoltage_V, current_mA);
    characteristic->setValue((uint8_t*)payload, strlen(payload));
    characteristic->notify();
  }

  delay(20);
}