/*
 * Part 3 - Deliverable 20, 21, 22
 * Read UID and print all pages of the NFC tag
 * 
 * This code reads the UID of an NTAG213 tag and prints out the memory pages.
 * Adjust MAX_PAGE to find the maximum page number on your tag.
 * 
 * NTAG213 has pages 0x00 to 0x2C (44 pages total = 176 bytes)
 * User-writable pages: 0x04 to 0x27 (36 pages = 144 bytes)
 */

#include <Wire.h>
#include <Adafruit_PN532.h>

// PN532 I2C pins (adjust if using SPI or different wiring)
#define PN532_IRQ   (2)
#define PN532_RESET (3)

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// Change this value to test different page ranges
// Start with a small number, then increase until you get errors
// NTAG213 max page is 0x2C (44 in decimal)
#define MAX_PAGE 0x30   // Try 0x04, 0x10, 0x2C, 0x30 to see behavior

void setup(void) {
  Serial.begin(115200);
  while (!Serial) delay(10);
  
  Serial.println("---------- NFC Tag Reader ----------");
  
  nfc.begin();
  
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Didn't find PN532 board");
    while (1); // halt
  }
  
  Serial.print("Found chip PN5"); 
  Serial.println((versiondata >> 24) & 0xFF, HEX);
  Serial.print("Firmware ver. "); 
  Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print('.'); 
  Serial.println((versiondata >> 8) & 0xFF, DEC);
  
  nfc.SAMConfig();
  
  Serial.println("Waiting for an NFC tag...");
}

void loop(void) {
  uint8_t success;
  uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0};  // up to 7 bytes for UID
  uint8_t uidLength;
  
  // Wait for an NFC tag
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  
  if (success) {
    Serial.println("");
    Serial.println("==================================");
    Serial.println("Tag detected!");
    Serial.print("UID Length: "); 
    Serial.print(uidLength, DEC); 
    Serial.println(" bytes");
    
    Serial.print("UID Value: ");
    for (uint8_t i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) Serial.print("0");
      Serial.print(uid[i], HEX);
      Serial.print(" ");
    }
    Serial.println("");
    Serial.println("==================================");
    
    // Print all pages from 0x00 up to MAX_PAGE
    Serial.println("Reading pages...");
    Serial.println("Page : Byte0 Byte1 Byte2 Byte3");
    Serial.println("---------------------------------");
    
    for (uint8_t page = 0x00; page <= MAX_PAGE; page++) {
      uint8_t data[4];
      success = nfc.ntag2xx_ReadPage(page, data);
      
      // Print page number
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
        Serial.println("READ FAIL (page does not exist)");
        break;  // Stop if we hit a non-existent page
      }
    }
    
    // Calculate BCC0 and BCC1 from the UID we read
    // For NTAG213: UID is 7 bytes, stored in pages 0 and 1
    // Page 0: UID0, UID1, UID2, BCC0
    // Page 1: UID3, UID4, UID5, UID6
    // Page 2: BCC1, internal, lock0, lock1
    if (uidLength == 7) {
      Serial.println("");
      Serial.println("---- BCC Calculation ----");
      uint8_t bcc0 = 0x88 ^ uid[0] ^ uid[1] ^ uid[2];
      uint8_t bcc1 = uid[3] ^ uid[4] ^ uid[5] ^ uid[6];
      
      Serial.print("BCC0 = 0x88 ^ UID0 ^ UID1 ^ UID2 = 0x");
      if (bcc0 < 0x10) Serial.print("0");
      Serial.println(bcc0, HEX);
      
      Serial.print("BCC1 = UID3 ^ UID4 ^ UID5 ^ UID6 = 0x");
      if (bcc1 < 0x10) Serial.print("0");
      Serial.println(bcc1, HEX);
      
      Serial.println("BCC0 is at Page 0x00, Byte 3");
      Serial.println("BCC1 is at Page 0x02, Byte 0");
    }
    
    Serial.println("");
    Serial.println("Remove tag and place again to re-read...");
    delay(3000);
  }
  
  delay(500);
}
