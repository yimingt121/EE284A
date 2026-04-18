#include <Wire.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"
#include <math.h>

Adafruit_BME680 bme;

const int NUM_SAMPLES = 5000;
const int N_SMALL = 50;
const int N_LARGE = 500;
const float ALPHA_SMALL = 2.0 / (N_SMALL + 1);
const float ALPHA_LARGE = 2.0 / (N_LARGE + 1);

// Store all raw samples (5000 floats = 20 KB — make sure your board has enough RAM!)
float rawSamples[NUM_SAMPLES];

// ---------- Stats ----------
struct Stats { float minVal, maxVal, mean, stdev; };

Stats computeStats(float *data, int n) {
  Stats s;
  s.minVal = 1e9; s.maxVal = -1e9;
  float mean = 0, M2 = 0;
  for (int i = 0; i < n; i++) {
    float x = data[i];
    if (x < s.minVal) s.minVal = x;
    if (x > s.maxVal) s.maxVal = x;
    float delta = x - mean;
    mean += delta / (i + 1);
    M2 += delta * (x - mean);
  }
  s.mean = mean;
  s.stdev = (n < 2) ? 0 : sqrt(M2 / (n - 1));
  return s;
}

void printStats(const char *label, Stats s) {
  Serial.print(F("\n=== ")); Serial.print(label); Serial.println(F(" ==="));
  Serial.print(F("Min:  ")); Serial.println(s.minVal, 4);
  Serial.print(F("Max:  ")); Serial.println(s.maxVal, 4);
  Serial.print(F("Mean: ")); Serial.println(s.mean,   4);
  Serial.print(F("Std:  ")); Serial.println(s.stdev,  4);
}

// ---------- Filters (work on stored data, write to output array) ----------
void applyMovingAverage(float *in, float *out, int n, int N) {
  float sum = 0;
  for (int i = 0; i < n; i++) {
    sum += in[i];
    if (i >= N) sum -= in[i - N];
    int count = (i < N) ? (i + 1) : N;
    out[i] = sum / count;
  }
}

void applyMovingMedian(float *in, float *out, int n, int N) {
  float *window = (float*) malloc(N * sizeof(float));
  float *sorted = (float*) malloc(N * sizeof(float));
  for (int i = 0; i < n; i++) {
    int count = (i < N) ? (i + 1) : N;
    // Fill window from last `count` samples
    for (int j = 0; j < count; j++) sorted[j] = in[i - count + 1 + j];
    // Insertion sort
    for (int a = 1; a < count; a++) {
      float key = sorted[a]; int b = a - 1;
      while (b >= 0 && sorted[b] > key) { sorted[b+1] = sorted[b]; b--; }
      sorted[b+1] = key;
    }
    if (count % 2 == 1) out[i] = sorted[count/2];
    else out[i] = 0.5 * (sorted[count/2 - 1] + sorted[count/2]);
  }
  free(window); free(sorted);
}

void applyEMA(float *in, float *out, int n, float alpha) {
  out[0] = in[0];
  for (int i = 1; i < n; i++) {
    out[i] = alpha * in[i] + (1.0 - alpha) * out[i-1];
  }
}

// ---------- Setup & Loop ----------
void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println(F("BME688 Filter Comparison"));

  if (!bme.begin()) {
    Serial.println(F("Sensor not found!"));
    while (1);
  }

  bme.setTemperatureOversampling(BME680_OS_1X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_0);
  bme.setHumidityOversampling(BME680_OS_NONE);
  bme.setPressureOversampling(BME680_OS_NONE);
  bme.setGasHeater(0, 0);

  Serial.println(F("Sensor OK. Collecting 5000 samples..."));

  // ---- Phase 1: Collect samples ----
  unsigned long startTime = millis();
  for (int i = 0; i < NUM_SAMPLES; i++) {
    while (!bme.performReading()) { delay(10); }
    rawSamples[i] = bme.temperature;

    if ((i + 1) % 500 == 0) {
      Serial.print(F("  "));
      Serial.print(i + 1);
      Serial.print(F("/5000 ("));
      Serial.print((millis() - startTime) / 1000);
      Serial.println(F("s)"));
    }
  }
  Serial.print(F("Collection done in "));
  Serial.print((millis() - startTime) / 1000);
  Serial.println(F(" seconds."));

  // ---- Phase 2: Run all filters ----
  float *filtered = (float*) malloc(NUM_SAMPLES * sizeof(float));
  if (!filtered) {
    Serial.println(F("Out of memory!"));
    while(1);
  }

  // 1. No filter
  printStats("No filtering", computeStats(rawSamples, NUM_SAMPLES));

  // 2. Moving Average N=50
  applyMovingAverage(rawSamples, filtered, NUM_SAMPLES, N_SMALL);
  printStats("Moving Average N=50", computeStats(filtered, NUM_SAMPLES));

  // 3. Moving Average N=500
  applyMovingAverage(rawSamples, filtered, NUM_SAMPLES, N_LARGE);
  printStats("Moving Average N=500", computeStats(filtered, NUM_SAMPLES));

  // 4. Moving Median N=50
  applyMovingMedian(rawSamples, filtered, NUM_SAMPLES, N_SMALL);
  printStats("Moving Median N=50", computeStats(filtered, NUM_SAMPLES));

  // 5. Moving Median N=500
  applyMovingMedian(rawSamples, filtered, NUM_SAMPLES, N_LARGE);
  printStats("Moving Median N=500", computeStats(filtered, NUM_SAMPLES));

  // 6. EMA small
  applyEMA(rawSamples, filtered, NUM_SAMPLES, ALPHA_SMALL);
  printStats("EMA alpha=2/(50+1)", computeStats(filtered, NUM_SAMPLES));

  // 7. EMA large
  applyEMA(rawSamples, filtered, NUM_SAMPLES, ALPHA_LARGE);
  printStats("EMA alpha=2/(500+1)", computeStats(filtered, NUM_SAMPLES));

  free(filtered);
  Serial.println(F("\n=== All done ==="));
}

void loop() {}