#include <Wire.h> //The package for I2C
#include <Adafruit_PN532.h>

// -----------------------------
// PN532
// -----------------------------
// We do not connect these pins but they must be input into the PN532 object
#define PN532_IRQ   -1
#define PN532_RESET -1   

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET, &Wire);


void printUID(const uint8_t *uid, uint8_t uidLength) {
  for (uint8_t i = 0; i < uidLength; i++) {
    if (uid[i] < 0x10) Serial.print("0");
    Serial.print(uid[i], HEX);
    if (i + 1 < uidLength) Serial.print(" ");
  }
}

bool readPageSpan(uint8_t startPage, uint8_t endPage) {
  uint8_t buf[4];

  if (startPage > endPage || endPage > 0x2C) {
    Serial.println("OUT OF RANGE");
    return false;
  }

  for (uint8_t page = startPage; page <= endPage; page++) {
    if (page < 0x10) Serial.print("0");
    Serial.print(page, HEX);
    Serial.print(": ");

    if (nfc.ntag2xx_ReadPage(page, buf)) {
      for (uint8_t i = 0; i < 4; i++) {
        if (buf[i] < 0x10) Serial.print("0");
        Serial.print(buf[i], HEX);
        if (i < 3) Serial.print(" ");
      }
      Serial.println();
    } else {
      Serial.println("READ FAIL");
      return false;
    }
  }

  return true;
}


void setup() {

  Serial.begin(115200);
  while (!Serial) delay(10);

  Wire.begin();

  nfc.begin(); 

  uint32_t versiondata = nfc.getFirmwareVersion();  
  if (!versiondata) {
    Serial.println("ERROR: PN532 not found. Check I2C mode + wiring (and IRQ pin if using Adafruit lib I2C).");
    while (1) delay(10);
  }

  Serial.print("Found chip PN5");
  Serial.println((versiondata >> 24) & 0xFF, HEX);
  Serial.print("Firmware ver. ");
  Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print(".");
  Serial.println((versiondata >> 8) & 0xFF, DEC);

  
  nfc.SAMConfig();

  Serial.println("Waiting for tag...");
}

void loop() {
  uint8_t uid[7];  
  uint8_t uidLength = 0;

  bool success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100);

  if (success) {
    Serial.print("UID length: ");
    Serial.print(uidLength);
    Serial.print(" bytes | UID: ");
    printUID(uid, uidLength);
    Serial.println();
    Serial.println("Pages:");
    readPageSpan(0x00, 0x27); 
    Serial.println();


    delay(500);
  }
}