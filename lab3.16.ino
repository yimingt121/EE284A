#include <Wire.h>
#include <Adafruit_PN532.h>

#define PN532_IRQ   -1
#define PN532_RESET -1   

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET, &Wire);

const uint32_t POLL_TIMEOUT_MS = 250;
const uint32_t INTER_TRIAL_DELAY_MS = 80;
const int N_TRIALS = 30;

bool onePoll(uint32_t &ttf_ms) {
  uint8_t uid[7];
  uint8_t uidLength = 0;
  uint32_t t0 = millis();
  bool ok = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, POLL_TIMEOUT_MS);
  ttf_ms = millis() - t0;
  return ok;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Wire.begin();
  nfc.begin(); 

  uint32_t versiondata = nfc.getFirmwareVersion();  
  if (!versiondata) {
    Serial.println("ERROR: PN532 not found.");
    while (1) delay(10);
  }

  nfc.SAMConfig();

  Serial.println("--- NFC DISTANCE EXPERIMENT ---");
  Serial.println("Instructions: Position your tag, then type the distance (e.g., 5.0) and press Enter.");
  Serial.println("Format: Distance(cm), Orientation, Successes, SuccessRate(%), AvgTTF(ms)");
  Serial.println("---------------------------------------------------------");
}

void loop() {
  if (Serial.available() > 0) {
    // Read the distance input from user
    String distanceInput = Serial.readStringUntil('\n');
    distanceInput.trim();
    
    if (distanceInput.length() > 0) {
      Serial.println("Running 30 trials... Keep tag steady.");
      
      uint32_t totalTtfSuccessful = 0;
      int successCount = 0;

      for (int i = 0; i < N_TRIALS; i++) {
        uint32_t currentTtf = 0;
        if (onePoll(currentTtf)) {
          successCount++;
          totalTtfSuccessful += currentTtf;
        }
        delay(INTER_TRIAL_DELAY_MS);
      }

      // Calculations
      float successRate = ((float)successCount / N_TRIALS) * 100.0;
      float avgTtf = (successCount > 0) ? (float)totalTtfSuccessful / successCount : 0;

      // Print as a CSV row for easy copy-pasting
      Serial.println("\n--- DATA ROW ---");
      Serial.print(distanceInput);      // Distance
      Serial.print(", ");
      Serial.print("TBD");              // Placeholder for Orientation (Label it manually in your notes)
      Serial.print(", ");
      Serial.print(successCount);       // Successes
      Serial.print(", ");
      Serial.print(successRate, 1);     // Success Rate %
      Serial.print(", ");
      Serial.println(avgTtf, 2);        // Avg Time
      Serial.println("----------------\n");
      
      Serial.println("Ready for next measurement. Enter Distance:");
    }
  }
}