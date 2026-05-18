#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ============================================================
// Lab 4 - Power-Aware Fan Control
// Deliverables addressed:
//   12: control strategy (hysteretic voltage-band, see sampleAndControl)
//   13: V / I / P / fan state logged at 20 ms and averaged at 10 s
//   14: SoC before/after via voltage AND coulomb count; fan-on %;
//       energy balance with a CALIBRATED fan power model
// ============================================================

// ====== BLE ======
#define SERVICE_UUID    "12345678-1234-1234-1234-1234567890ab"
#define CHAR_SUMMARY    "12345678-1234-1234-1234-1234567890ac"
#define CHAR_RAWBURST   "12345678-1234-1234-1234-1234567890ad"

BLECharacteristic *summaryChar;
BLECharacteristic *rawChar;
bool connected = false;
bool restartAdvertising = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*)    { connected = true;  Serial.println("# Client connected"); }
  void onDisconnect(BLEServer*) { connected = false; restartAdvertising = true;
                                  Serial.println("# Client disconnected"); }
};

// ====== Pins / sensors ======
const int FAN_PIN  = A0;
const int PWM_FREQ = 25000;
const int PWM_RES  = 8;

Adafruit_INA219 ina_batt(0x40);

// ====== Control parameters ======
const float V_MIN   = 3.30;
const float V_LOW   = 3.50;
const float V_HIGH  = 3.70;
const float V_HYST  = 0.05;

const int DUTY_OFF  = 0;
const int DUTY_25   = 64;
const int DUTY_50   = 128;
const int DUTY_100  = 255;

// ====== Current sign ======
// + = charging into battery. Auto-detected at calibration: while
// the fan is running off the battery (panel covered) we EXPECT
// discharge, so the raw sign that gives negative current is "charging+".
float I_SIGN = 1.0;

// ====== Timing ======
const unsigned long SAMPLE_INTERVAL_MS    = 20;
const unsigned long REPORT_INTERVAL_MS    = 10000;
const unsigned long PHASE_DURATION_MS     = 300000UL;   // 5 min sun, 5 min dark
const unsigned long EXPERIMENT_DURATION_MS = 2 * PHASE_DURATION_MS;
const int SAMPLES_PER_WINDOW = REPORT_INTERVAL_MS / SAMPLE_INTERVAL_MS;

unsigned long lastSample = 0;
unsigned long lastReport = 0;
unsigned long experimentStartMs = 0;

// ====== Fan calibration table ======
// Filled by calibrateFan() at startup. Power in mW measured as the
// magnitude of battery discharge while fan runs and panel is COVERED.
float fanPowerTable[4] = {0, 0, 0, 0};   // index: 0=off, 1=25%, 2=50%, 3=100%
const int   dutyLevels[4] = {DUTY_OFF, DUTY_25, DUTY_50, DUTY_100};

float fanPowerForDuty(int duty) {
  for (int k = 0; k < 4; k++) if (duty == dutyLevels[k]) return fanPowerTable[k];
  // linear interp fallback (shouldn't be needed since we only use 4 levels)
  return fanPowerTable[3] * duty / 255.0f;
}

// ====== Raw-sample BLE buffers ======
uint16_t rawV_mV[SAMPLES_PER_WINDOW];
int16_t  rawI_dA[SAMPLES_PER_WINDOW];
uint8_t  rawDuty[SAMPLES_PER_WINDOW];
int sampleIdx = 0;

// ====== Window accumulators (10 s) ======
float sumVolt = 0, sumCurrent = 0, sumPower = 0, sumHarvest = 0, sumLoad = 0;
int   sampleCount = 0;
int   fanOnSamples = 0;

// ====== Cumulative experiment stats ======
float  startVoltage = -1, endVoltage = 0;
double totalEnergyHarvested_mJ = 0;
double totalEnergyConsumed_mJ  = 0;
double chargeIn_mAs  = 0;     // coulombs into battery  (iBatt > 0)
double chargeOut_mAs = 0;     // coulombs out of battery (iBatt < 0)
unsigned long totalFanOnMs = 0;

// Phase-separated stats (so the report can split day vs night)
struct PhaseStats {
  double Eharv_mJ = 0;
  double Econs_mJ = 0;
  double Qin_mAs  = 0;
  double Qout_mAs = 0;
  unsigned long fanOnMs = 0;
  float vStart = -1, vEnd = 0;
  unsigned long samples = 0;
};
PhaseStats sunPhase, darkPhase;

bool finalPrinted = false;
int  currentDuty = 0;

// Forward decls
void calibrateFan();
void sampleAndControl(unsigned long now);
void reportWindow(unsigned long now);
void printFinalSummary(unsigned long now);
void printPhase(const char* name, const PhaseStats &ps);
void sendAveragedBLE(unsigned long now, float v, float i, float p,
                     float h, float ld, int duty, float fanDuty, uint8_t phase);
void sendRawBurstBLE(uint16_t *v, int16_t *i, uint8_t *d, int n);

// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  ledcAttach(FAN_PIN, PWM_FREQ, PWM_RES);
  ledcWrite(FAN_PIN, 0);

  if (!ina_batt.begin()) {
    Serial.println("# INA219 not found");
    while (1) delay(10);
  }
  ina_batt.setCalibration_16V_400mA();
  Serial.println("# INA219 ready");

  // ---- BLE init ----
  BLEDevice::init("ESP32-Lab4-YT");
  BLEDevice::setPower(ESP_PWR_LVL_P9);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService *service = server->createService(SERVICE_UUID);
  summaryChar = service->createCharacteristic(CHAR_SUMMARY, BLECharacteristic::PROPERTY_NOTIFY);
  summaryChar->addDescriptor(new BLE2902());
  rawChar    = service->createCharacteristic(CHAR_RAWBURST, BLECharacteristic::PROPERTY_NOTIFY);
  rawChar->addDescriptor(new BLE2902());
  service->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("# BLE advertising started");

  // ---- Fan calibration ----
  // IMPORTANT: cover the solar panel before running this. Calibration
  // measures battery discharge at each duty level, which is the fan's
  // true power consumption only if the panel is contributing zero.
  Serial.println(F("# === FAN CALIBRATION ==="));
  Serial.println(F("# Cover the solar panel NOW. Calibration starts in 5 s."));
  delay(5000);
  calibrateFan();
  Serial.println(F("# Calibration done. Uncover the panel and start the experiment in 10 s."));
  delay(10000);

  // CSV header
  Serial.println(F("# RAW DATA (20 ms)"));
  Serial.println(F("t_ms,phase,vBatt_V,iBatt_mA,pBatt_mW,pHarvest_mW,pLoad_mW,duty"));

  experimentStartMs = millis();
}

// ============================================================
void loop() {
  if (restartAdvertising) {
    delay(200);
    BLEDevice::startAdvertising();
    Serial.println("# Advertising restarted");
    restartAdvertising = false;
  }

  unsigned long now = millis();

  if (now - lastSample >= SAMPLE_INTERVAL_MS) {
    lastSample = now;
    sampleAndControl(now);
  }
  if (now - lastReport >= REPORT_INTERVAL_MS) {
    lastReport = now;
    reportWindow(now);
  }
  if (!finalPrinted && (now - experimentStartMs >= EXPERIMENT_DURATION_MS)) {
    finalPrinted = true;
    ledcWrite(FAN_PIN, 0);
    currentDuty = 0;
    printFinalSummary(now);
  }
}

// ============================================================
// Fan calibration: measure fan power at each duty level by reading
// battery discharge while the panel is covered. Also auto-detects
// the current sign convention.
// ============================================================
void calibrateFan() {
  // First, check sign with fan OFF: idle current should be small but
  // negative (battery powering MCU). We'll set I_SIGN so that
  // discharge reads negative.
  ledcWrite(FAN_PIN, DUTY_100);
  delay(2000);                       // let fan spin up and current settle
  float iRaw = 0;
  for (int k = 0; k < 50; k++) { iRaw += ina_batt.getCurrent_mA(); delay(20); }
  iRaw /= 50.0f;
  // At 100% with panel covered, current must be discharge (negative
  // in our convention). If raw reads positive, flip the sign.
  I_SIGN = (iRaw > 0) ? -1.0f : 1.0f;
  Serial.print(F("# Auto sign detect: raw I at 100% = "));
  Serial.print(iRaw, 2); Serial.print(F(" mA  -> I_SIGN = "));
  Serial.println(I_SIGN, 0);

  // Now measure power at each level
  for (int k = 0; k < 4; k++) {
    int duty = dutyLevels[k];
    ledcWrite(FAN_PIN, duty);
    delay(2000);                     // settle

    float sumV = 0, sumI = 0;
    const int N = 100;               // 100 samples * 20 ms = 2 s average
    for (int n = 0; n < N; n++) {
      float v = ina_batt.getBusVoltage_V();
      float i = ina_batt.getCurrent_mA() * I_SIGN;
      sumV += v;
      sumI += i;
      delay(20);
    }
    float vAvg = sumV / N;
    float iAvg = sumI / N;           // negative = discharge
    float pDischarge = -vAvg * iAvg; // magnitude of battery draw

    fanPowerTable[k] = pDischarge;   // mW (includes MCU baseline at OFF)

    Serial.print(F("# Duty ")); Serial.print(duty);
    Serial.print(F(" : V=")); Serial.print(vAvg, 3);
    Serial.print(F(" V, I=")); Serial.print(iAvg, 2);
    Serial.print(F(" mA, P=")); Serial.print(pDischarge, 1);
    Serial.println(F(" mW"));
  }

  // Subtract baseline (MCU + radio) so fanPowerTable reflects FAN-only power
  float baseline = fanPowerTable[0];
  for (int k = 0; k < 4; k++) fanPowerTable[k] -= baseline;
  if (fanPowerTable[0] < 0) fanPowerTable[0] = 0;
  Serial.print(F("# Baseline (no-fan) draw: ")); Serial.print(baseline, 1); Serial.println(F(" mW"));

  ledcWrite(FAN_PIN, 0);
  currentDuty = 0;
}

// ============================================================
// 20 ms loop: sample, apply CONTROL POLICY, log + buffer
// ============================================================
void sampleAndControl(unsigned long now) {
  float vBatt = ina_batt.getBusVoltage_V();
  float iBatt = ina_batt.getCurrent_mA() * I_SIGN;     // + = charging
  float pBatt = vBatt * iBatt;                         // + = into battery

  // Load = calibrated fan power for current duty (mW)
  float pLoad = fanPowerForDuty(currentDuty);

  // Harvest by energy balance:
  //   pBatt = pHarvest - pLoad   =>   pHarvest = pLoad + pBatt
  // pBatt is signed: charging (+) means harvest exceeds load.
  float pHarvest = pLoad + pBatt;
  if (pHarvest < 0) pHarvest = 0;

  // Determine phase (sun vs dark) by elapsed time
  unsigned long tElapsed = now - experimentStartMs;
  bool sunPhaseNow = (tElapsed < PHASE_DURATION_MS);
  PhaseStats &ps = sunPhaseNow ? sunPhase : darkPhase;
  uint8_t phaseTag = sunPhaseNow ? 0 : 1;

  // ===== CONTROL POLICY =====
  // Hysteretic voltage-band controller. The battery voltage is the
  // integral of energy balance, so holding the band keeps harvest >=
  // load on average. Hard floor at V_MIN protects the cell.
  int newDuty = currentDuty;
  if (vBatt < V_MIN) {
    newDuty = DUTY_OFF;                                // protect battery
  } else {
    // Downward (voltage falling)
    if      (vBatt < V_MIN  + V_HYST)            newDuty = DUTY_25;
    else if (vBatt < V_LOW  - V_HYST)            newDuty = DUTY_25;
    else if (vBatt < V_HIGH - V_HYST)            newDuty = DUTY_50;
    // Upward (voltage rising)
    if      (vBatt > V_HIGH + V_HYST)            newDuty = DUTY_100;
    else if (vBatt > V_LOW  + V_HYST && currentDuty < DUTY_50) newDuty = DUTY_50;
  }
  if (newDuty != currentDuty) {
    ledcWrite(FAN_PIN, newDuty);
    currentDuty = newDuty;
  }
  // ===== END CONTROL POLICY =====

  // Cumulative stats
  if (startVoltage < 0) startVoltage = vBatt;
  endVoltage = vBatt;
  float dt_s = SAMPLE_INTERVAL_MS / 1000.0f;
  totalEnergyHarvested_mJ += pHarvest * dt_s;
  totalEnergyConsumed_mJ  += pLoad    * dt_s;
  if (iBatt > 0) chargeIn_mAs  += iBatt * dt_s;
  else           chargeOut_mAs += -iBatt * dt_s;
  if (currentDuty > 0) totalFanOnMs += SAMPLE_INTERVAL_MS;

  // Phase stats
  if (ps.vStart < 0) ps.vStart = vBatt;
  ps.vEnd = vBatt;
  ps.Eharv_mJ += pHarvest * dt_s;
  ps.Econs_mJ += pLoad    * dt_s;
  if (iBatt > 0) ps.Qin_mAs  += iBatt * dt_s;
  else           ps.Qout_mAs += -iBatt * dt_s;
  if (currentDuty > 0) ps.fanOnMs += SAMPLE_INTERVAL_MS;
  ps.samples++;

  // Window accumulators (10 s averages)
  sumVolt    += vBatt;
  sumCurrent += iBatt;
  sumPower   += pBatt;
  sumHarvest += pHarvest;
  sumLoad    += pLoad;
  sampleCount++;
  if (currentDuty > 0) fanOnSamples++;

  // Serial CSV (raw 20 ms)
  Serial.print(now);          Serial.print(',');
  Serial.print(phaseTag);     Serial.print(',');
  Serial.print(vBatt, 4);     Serial.print(',');
  Serial.print(iBatt, 2);     Serial.print(',');
  Serial.print(pBatt, 2);     Serial.print(',');
  Serial.print(pHarvest, 2);  Serial.print(',');
  Serial.print(pLoad, 2);     Serial.print(',');
  Serial.println(currentDuty);

  // Buffer for BLE
  if (sampleIdx < SAMPLES_PER_WINDOW) {
    rawV_mV[sampleIdx] = (uint16_t)(vBatt * 1000.0f);
    rawI_dA[sampleIdx] = (int16_t)(iBatt * 10.0f);
    rawDuty[sampleIdx] = (uint8_t)currentDuty;
    sampleIdx++;
  }
}

// ============================================================
// 10 s averages + BLE
// ============================================================
void reportWindow(unsigned long now) {
  if (sampleCount == 0) return;
  float avgV = sumVolt    / sampleCount;
  float avgI = sumCurrent / sampleCount;
  float avgP = sumPower   / sampleCount;
  float avgH = sumHarvest / sampleCount;
  float avgL = sumLoad    / sampleCount;
  float fanDuty = (float)fanOnSamples / sampleCount;

  unsigned long tElapsed = now - experimentStartMs;
  uint8_t phaseTag = (tElapsed < PHASE_DURATION_MS) ? 0 : 1;

  sendAveragedBLE(now, avgV, avgI, avgP, avgH, avgL, currentDuty, fanDuty, phaseTag);
  sendRawBurstBLE(rawV_mV, rawI_dA, rawDuty, sampleIdx);

  Serial.print(F("# t=")); Serial.print(now/1000);
  Serial.print(F("s phase=")); Serial.print(phaseTag == 0 ? F("SUN") : F("DARK"));
  Serial.print(F(" V="));      Serial.print(avgV, 3);
  Serial.print(F(" I="));      Serial.print(avgI, 1);
  Serial.print(F(" P="));      Serial.print(avgP, 1);
  Serial.print(F(" Pharv="));  Serial.print(avgH, 1);
  Serial.print(F(" Pload="));  Serial.print(avgL, 1);
  Serial.print(F(" duty="));   Serial.print(fanDuty*100, 0);
  Serial.print(F("% PWM="));   Serial.println(currentDuty);

  sumVolt = sumCurrent = sumPower = sumHarvest = sumLoad = 0;
  sampleCount = fanOnSamples = 0;
  sampleIdx = 0;
}

// ============================================================
void sendAveragedBLE(unsigned long now, float v, float i, float p,
                     float h, float ld, int duty, float fanDuty, uint8_t phase) {
  if (!connected) return;
  char payload[140];
  snprintf(payload, sizeof(payload),
           "AVG,%lu,%u,%.3f,%.2f,%.2f,%.2f,%.2f,%d,%.2f",
           now, phase, v, i, p, h, ld, duty, fanDuty);
  summaryChar->setValue((uint8_t*)payload, strlen(payload));
  summaryChar->notify();
}

void sendRawBurstBLE(uint16_t *v, int16_t *i, uint8_t *d, int n) {
  if (!connected || n <= 0) return;
  const int SPP = 4, BPS = 5;
  uint8_t pkt[SPP * BPS];
  for (int s = 0; s < n; s += SPP) {
    int count = min(SPP, n - s);
    for (int k = 0; k < count; k++) {
      uint16_t vv = v[s + k];
      int16_t  ii = i[s + k];
      pkt[k*5 + 0] = vv & 0xFF;
      pkt[k*5 + 1] = (vv >> 8) & 0xFF;
      pkt[k*5 + 2] = ii & 0xFF;
      pkt[k*5 + 3] = (ii >> 8) & 0xFF;
      pkt[k*5 + 4] = d[s + k];
    }
    rawChar->setValue(pkt, count * BPS);
    rawChar->notify();
    delay(8);
  }
}

// ============================================================
void printPhase(const char* name, const PhaseStats &ps) {
  float dur_s = ps.samples * (SAMPLE_INTERVAL_MS / 1000.0f);
  float fanPct = (dur_s > 0) ? 100.0f * ps.fanOnMs / 1000.0f / dur_s : 0;
  Serial.print(F("# --- ")); Serial.print(name); Serial.println(F(" phase ---"));
  Serial.print(F("#  V start/end:     ")); Serial.print(ps.vStart, 3);
  Serial.print(F(" -> "));                 Serial.print(ps.vEnd, 3);
  Serial.print(F(" V (dV="));              Serial.print(ps.vEnd - ps.vStart, 3); Serial.println(F(")"));
  Serial.print(F("#  Energy harvest:  ")); Serial.print(ps.Eharv_mJ, 1); Serial.println(F(" mJ"));
  Serial.print(F("#  Energy consume:  ")); Serial.print(ps.Econs_mJ, 1); Serial.println(F(" mJ"));
  Serial.print(F("#  Net energy:      ")); Serial.print(ps.Eharv_mJ - ps.Econs_mJ, 1); Serial.println(F(" mJ"));
  Serial.print(F("#  Charge in:       ")); Serial.print(ps.Qin_mAs / 3.6, 3);  Serial.println(F(" mAh"));
  Serial.print(F("#  Charge out:      ")); Serial.print(ps.Qout_mAs / 3.6, 3); Serial.println(F(" mAh"));
  Serial.print(F("#  Net charge:      ")); Serial.print((ps.Qin_mAs - ps.Qout_mAs) / 3.6, 3); Serial.println(F(" mAh"));
  Serial.print(F("#  Fan on:          ")); Serial.print(ps.fanOnMs / 1000.0, 1);
  Serial.print(F(" s ("));                 Serial.print(fanPct, 1); Serial.println(F("%)"));
}

// ============================================================
void printFinalSummary(unsigned long now) {
  unsigned long elapsed = now - experimentStartMs;
  float fanOnPct = 100.0f * totalFanOnMs / (float)elapsed;
  double netEnergy_mJ = totalEnergyHarvested_mJ - totalEnergyConsumed_mJ;
  double netCharge_mAh = (chargeIn_mAs - chargeOut_mAs) / 3.6;
  float deltaV = endVoltage - startVoltage;

  Serial.println();
  Serial.println(F("# ============================================"));
  Serial.println(F("# ========== EXPERIMENT SUMMARY =============="));
  Serial.println(F("# ============================================"));

  Serial.println(F("# Fan power calibration (mW):"));
  for (int k = 0; k < 4; k++) {
    Serial.print(F("#   duty ")); Serial.print(dutyLevels[k]);
    Serial.print(F(" -> "));      Serial.print(fanPowerTable[k], 1);
    Serial.println(F(" mW"));
  }
  Serial.println(F("#"));

  printPhase("SUN", sunPhase);
  printPhase("DARK", darkPhase);

  Serial.println(F("#"));
  Serial.println(F("# --- OVERALL ---"));
  Serial.print(F("# Duration:           ")); Serial.print(elapsed / 1000.0, 1); Serial.println(F(" s"));
  Serial.print(F("# V start / end:      ")); Serial.print(startVoltage, 3);
  Serial.print(F(" -> "));                   Serial.print(endVoltage, 3);
  Serial.print(F(" V (dV="));                Serial.print(deltaV, 3); Serial.println(F(")"));
  Serial.print(F("# Battery preserved:  ")); Serial.println(deltaV >= 0 ? F("YES (V)") : F("NO (V)"));
  Serial.print(F("# Net charge:         ")); Serial.print(netCharge_mAh, 3); Serial.println(F(" mAh"));
  Serial.print(F("# Charge preserved:   ")); Serial.println(netCharge_mAh >= 0 ? F("YES (Q)") : F("NO (Q)"));
  Serial.print(F("# Energy harvested:   ")); Serial.print(totalEnergyHarvested_mJ, 1); Serial.println(F(" mJ"));
  Serial.print(F("# Energy consumed:    ")); Serial.print(totalEnergyConsumed_mJ, 1);  Serial.println(F(" mJ"));
  Serial.print(F("# Net energy:         ")); Serial.print(netEnergy_mJ, 1);            Serial.println(F(" mJ"));
  Serial.print(F("# Harvest >= consume? ")); Serial.println(netEnergy_mJ >= 0 ? F("YES") : F("NO"));
  Serial.print(F("# Fan on time:        ")); Serial.print(totalFanOnMs / 1000.0, 1); Serial.println(F(" s"));
  Serial.print(F("# Fan on percentage:  ")); Serial.print(fanOnPct, 1); Serial.println(F(" %"));
  Serial.println(F("# ============================================"));

  if (connected) {
    char payload[200];
    snprintf(payload, sizeof(payload),
             "DONE,V0=%.3f,V1=%.3f,dQ=%.3fmAh,Eh=%.1f,Ec=%.1f,fan%%=%.1f",
             startVoltage, endVoltage, (float)netCharge_mAh,
             (float)totalEnergyHarvested_mJ, (float)totalEnergyConsumed_mJ, fanOnPct);
    summaryChar->setValue((uint8_t*)payload, strlen(payload));
    summaryChar->notify();
  }
}
