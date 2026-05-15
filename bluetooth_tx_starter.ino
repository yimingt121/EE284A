#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID "12345678-1234-1234-1234-1234567890ab"
#define CHAR_UUID    "12345678-1234-1234-1234-1234567890ac"

Adafruit_INA219 ina219;
BLECharacteristic *characteristic;
bool connected = false;
bool restartAdvertising = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*)    { connected = true;  Serial.println("Client connected"); }
  void onDisconnect(BLEServer*) { connected = false; restartAdvertising = true;
                                  Serial.println("Client disconnected"); }
};

void setup() {
  Serial.begin(115200);
  delay(500);

  // ---- INA219 init ----
  if (!ina219.begin()) {
    Serial.println("Failed to find INA219 chip");
    while (1) delay(10);
  }
  // Default calibration is 32V / 2A; use setCalibration_16V_400mA() for
  // better resolution at LiPo voltages and small currents.
  ina219.setCalibration_16V_400mA();
  Serial.println("INA219 ready");

  // ---- BLE init ----
  BLEDevice::init("ESP32-Lab4-YT");   // <-- change this
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
}

void loop() {
  if (restartAdvertising) {
    delay(200);
    BLEDevice::startAdvertising();
    Serial.println("Advertising restarted");
    restartAdvertising = false;
  }

  // ---- Read INA219 ----
  float shuntVoltage_mV = ina219.getShuntVoltage_mV();   // mV across the 0.1 Ω shunt
  float busVoltage_V    = ina219.getBusVoltage_V();      // V from Vin- to GND
  float current_mA      = ina219.getCurrent_mA();        // mA through the shunt
  float loadVoltage_V   = busVoltage_V + (shuntVoltage_mV / 1000.0);  // battery-terminal V

  // ---- Log to serial ----
  Serial.print("Bus: ");      Serial.print(busVoltage_V, 3);    Serial.print(" V  ");
  Serial.print("Shunt: ");    Serial.print(shuntVoltage_mV, 3); Serial.print(" mV  ");
  Serial.print("Load (batt): "); Serial.print(loadVoltage_V, 3); Serial.print(" V  ");
  Serial.print("I: ");        Serial.print(current_mA, 3);      Serial.println(" mA");

  // ---- Send over BLE ----
  if (connected) {
    char payload[64];
    snprintf(payload, sizeof(payload), "V=%.3f,I=%.3f", loadVoltage_V, current_mA);
    characteristic->setValue((uint8_t*)payload, strlen(payload));
    characteristic->notify();
  }

  delay(20);   // 20 ms sample period as specified
}