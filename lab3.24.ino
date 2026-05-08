/*
 * Part 3 - Deliverable 24
 * Write non-zero values to page 10 and page 24 (DECIMAL)
 * 
 * Page 10 (decimal) = 0x0A (hex)
 * Page 24 (decimal) = 0x18 (hex)
 * 
 * Both are within the user-writable range (0x04 to 0x27).
 * Then read all pages to verify the writes.
 */

#include <Wire.h>
#include <Adafruit_PN532.h>

#define PN532_IRQ   (2)
#define PN532_RESET (3)

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

bool tagProcessed = false;

void readAllPages() {
  Serial.println("");
  Serial.println("---- Reading all pages ----");
  Serial.println("Page (dec / hex) : Byte0 Byte1 Byte2 Byte3");
  Serial.println("------------------------------------------");
  
  for (uint8_t page = 0x00; page <= 0x2C; page++) {
    uint8_t data[4];
    bool success = nfc.ntag2xx_ReadPage(page, data);
    
    // Print page in decimal and hex
    if (page < 10) Serial.print(" ");
    Serial.print(page, DEC);
    Serial.print(" / 0x");
    if (page < 0x10) Serial.print("0");
    Serial.print(page, HEX);
    Serial.print("       : ");
    
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
  
  Serial.println("---------- NFC Tag Writer ----------");
  Serial.println("Will write to page 10 and page 24 (decimal)");
  
  nfc.begin();
  
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Didn't find PN532 board");
    while (1);
  }
  
  nfc.SAMConfig();
  Serial.println("Waiting for an NFC tag...");
}

void loop(void) {
  uint8_t success;
  uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0};
  uint8_t uidLength;
  
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  
  if (success && !tagProcessed) {
    Serial.println("");
    Serial.println("==================================");
    Serial.println("Tag detected!");
    Serial.println("==================================");
    
    // Data to write - any non-zero values
    // Page 10 will get: 0xDE 0xAD 0xBE 0xEF
    // Page 24 will get: 0xCA 0xFE 0xBA 0xBE
    uint8_t data10[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t data24[4] = {0xCA, 0xFE, 0xBA, 0xBE};
    
    // Write to page 10 (decimal)
    Serial.print("Writing to page 10 (decimal): ");
    bool ok10 = nfc.ntag2xx_WritePage(10, data10);
    Serial.println(ok10 ? "SUCCESS" : "FAIL");
    
    // Write to page 24 (decimal)
    Serial.print("Writing to page 24 (decimal): ");
    bool ok24 = nfc.ntag2xx_WritePage(24, data24);
    Serial.println(ok24 ? "SUCCESS" : "FAIL");
    
    // Verify by reading all pages
    readAllPages();
    
    Serial.println("");
    Serial.println("Notice values at page 0A (10 dec) and 18 (24 dec)");
    Serial.println("Done. Remove tag.");
    tagProcessed = true;
  }
  
  if (!success) {
    tagProcessed = false;
  }
  
  delay(500);
}
