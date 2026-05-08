/*
 * Part 3 - Deliverable 23
 * Clear the user-writable area of an NTAG213 tag
 * 
 * Writes 0x00 0x00 0x00 0x00 to all user pages (0x04 to 0x27)
 * Then reads back all pages to verify they are cleared.
 */

#include <Wire.h>
#include <Adafruit_PN532.h>

#define PN532_IRQ   (2)
#define PN532_RESET (3)

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

bool tagProcessed = false;

void clearNTAG213UserArea() {
  uint8_t blank[4] = {0x00, 0x00, 0x00, 0x00};
  for (uint8_t page = 0x04; page <= 0x27; page++) {
    bool ok = nfc.ntag2xx_WritePage(page, blank);
    if (page < 0x10) Serial.print("0");
    Serial.print(page, HEX);
    Serial.print(": ");
    if (ok) Serial.println("CLEARED");
    else Serial.println("WRITE FAIL");
  }
}

void readAllPages() {
  Serial.println("");
  Serial.println("---- Reading all pages ----");
  Serial.println("Page : Byte0 Byte1 Byte2 Byte3");
  Serial.println("---------------------------------");
  
  for (uint8_t page = 0x00; page <= 0x2C; page++) {
    uint8_t data[4];
    bool success = nfc.ntag2xx_ReadPage(page, data);
    
    if (page < 0x10) Serial.print("0");
    Serial.print(page, HEX);
    Serial.print("   : ");
    
    if (success) {
      for (uint8_t i = 0; i < 4; i++) {
        if (data[i] < 0x10) Serial.print("0");
        Serial.print(data[i], HEX);
        Serial.print("    ");
      }
      Serial.println("");
    } else {
      Serial.println("READ FAIL");
      break;
    }
  }
}

void setup(void) {
  Serial.begin(115200);
  while (!Serial) delay(10);
  
  Serial.println("---------- NFC Tag Clearer ----------");
  
  nfc.begin();
  
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Didn't find PN532 board");
    while (1);
  }
  
  nfc.SAMConfig();
  Serial.println("Waiting for an NFC tag to clear...");
}

void loop(void) {
  uint8_t success;
  uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0};
  uint8_t uidLength;
  
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  
  if (success && !tagProcessed) {
    Serial.println("");
    Serial.println("==================================");
    Serial.println("Tag detected! Clearing user area...");
    Serial.println("==================================");
    
    clearNTAG213UserArea();
    
    Serial.println("");
    Serial.println("Clearing complete. Verifying by reading...");
    readAllPages();
    
    Serial.println("");
    Serial.println("Done. Remove tag.");
    tagProcessed = true;
  }
  
  if (!success) {
    tagProcessed = false; // reset when tag is removed
  }
  
  delay(500);
}
