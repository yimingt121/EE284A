// #include <SPI.h>
// #include <Arduino.h>
// #include <RH_RF95.h>

// extern "C" uint8_t temprature_sens_read();

// // Link Budget Parameters
// #define RF_FREQUENCY          916.0
// #define TX_OUTPUT_POWER       20        
// #define LORA_BANDWIDTH        250000    
// #define LORA_SPREADING_FACTOR 11        
// #define LORA_CODINGRATE       5         // 5 = 4/5 (Required for 1638.4 chips)
// #define LORA_PREAMBLE_LENGTH  16        

// // Hardware Pins
// #define RFM_CS   33
// #define RFM_RST  27
// #define RFM_INT  4     // GPIO 4 (Label A5 on ESP32 Feather V2)

// RH_RF95 rf95(RFM_CS, RFM_INT);
// uint8_t txpacket[256];

// // SPI Write with Verification
// static void spiWriteReg(uint8_t addr, uint8_t val) {
//     SPISettings cfg(8000000, MSBFIRST, SPI_MODE0);
//     SPI.beginTransaction(cfg); 
//     digitalWrite(RFM_CS, LOW);
//     SPI.transfer(addr | 0x80); 
//     SPI.transfer(val);
//     digitalWrite(RFM_CS, HIGH); 
//     SPI.endTransaction();
// }

// void setup() {
//     Serial.begin(115200);
//     while (!Serial); 
    
//     pinMode(RFM_RST, OUTPUT);
//     digitalWrite(RFM_RST, HIGH);
    
//     if (!rf95.init()) { 
//         Serial.println("RFM95 init failed!"); 
//         while (1); 
//     }

//     // Set Modulation Parameters
//     rf95.setFrequency(RF_FREQUENCY);
//     rf95.setTxPower(TX_OUTPUT_POWER, false);
//     rf95.setSignalBandwidth(LORA_BANDWIDTH);
//     rf95.setSpreadingFactor(LORA_SPREADING_FACTOR);
//     rf95.setCodingRate4(LORA_CODINGRATE);
//     rf95.setPreambleLength(LORA_PREAMBLE_LENGTH);
//     rf95.setLowDatarate(); // Critical for SF11 robustness

//     // Set Sync Word (Must be done after init)
//     spiWriteReg(0x39, 0x2B);
//     Serial.println("LoRa Initialized. Frequency: 916MHz, Sync: 0x2B");
// }

// void loop() {
//     // --- PACKET CONSTRUCTION ---
//     const char* suid = "yimingt";
//     size_t txLen = 0;
//     auto append8    = [&](uint8_t v)  { txpacket[txLen++] = v; };
//     auto append32le = [&](uint32_t v) { for (int i = 0; i < 4; i++) append8((v >> (8*i)) & 0xFF); };

//     // Meshtastic Network Header (16 bytes)
//     append32le(0xFFFFFFFF);           // Destination (Broadcast)
//     memcpy(txpacket + 4, suid, 4);    // Source (First 4 bytes of your ID: 'yimi')
//     txLen += 4;
//     append32le(0x00000001);           // Packet ID
//     append8(0x63);                    // Flags: Hops remaining 3, Hop limit 3
//     append8(0x08);                    // Channel Hash
//     append8(0x00);                    // Next-Hop
//     append8(0x00);                    // Last-Hop

//     // Payload
//     float tempC = (temprature_sens_read() - 32) / 1.8f;
//     char msg[64];
//     snprintf(msg, sizeof(msg), "Temp: %.2f C from %s", tempC, suid);
//     for(int i=0; msg[i] != '\0'; i++) append8(msg[i]);

//     Serial.printf("\n[SEND] Sending %d bytes (ID: %s)... ", (int)txLen, suid);
//     rf95.send(txpacket, (uint8_t)txLen);

//     if (rf95.waitPacketSent(2000)) {
//         Serial.println("Success! Listening for 5s...");
        
//         // --- RECEIVE LOGIC ---
//         uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
//         uint8_t len = sizeof(buf);
//         if (rf95.waitAvailableTimeout(5000)) { 
//             if (rf95.recv(buf, &len)) {
//                 // Null-terminate safely to prevent garbage characters
//                 if (len < RH_RF95_MAX_MESSAGE_LEN) buf[len] = '\0'; 
                
//                 Serial.printf("[REPLY] Length: %d, RSSI: %d\n", len, rf95.lastRssi());
//                 if (len > 16) {
//                     Serial.print("Repeater Message: ");
//                     Serial.println((char*)buf + 16); 
//                 }
//             }
//         } else {
//             Serial.println("No reply received.");
//         }
//     } else {
//         Serial.println("Transmit failed.");
//     }

//     delay(10000); // Wait 10 seconds before sending again
// }


#include <SPI.h>
#include <Arduino.h>
#include <RH_RF95.h>

// Link the internal ESP32 temperature function
extern "C" uint8_t temprature_sens_read();

// --- Hardware Pins & Config ---
#define ADC_PIN A2 
#define RFM_CS   33
#define RFM_RST  27
#define RFM_INT  4     

// --- LoRa Parameters ---
#define RF_FREQUENCY          916.0
#define TX_OUTPUT_POWER       20        
#define LORA_BANDWIDTH        250000    
#define LORA_SPREADING_FACTOR 11        
#define LORA_CODINGRATE       5         
#define LORA_PREAMBLE_LENGTH  16        

RH_RF95 rf95(RFM_CS, RFM_INT);
uint8_t txpacket[256];

// Helper to write to LoRa registers
static void spiWriteReg(uint8_t addr, uint8_t val) {
    SPISettings cfg(8000000, MSBFIRST, SPI_MODE0);
    SPI.beginTransaction(cfg); 
    digitalWrite(RFM_CS, LOW);
    SPI.transfer(addr | 0x80); 
    SPI.transfer(val);
    digitalWrite(RFM_CS, HIGH); 
    SPI.endTransaction();
}

void setup() {
    Serial.begin(115200);
    while (!Serial); 
    
    // Set ADC to 12-bit for TMP36 sensor
    analogReadResolution(12); 
    
    pinMode(RFM_RST, OUTPUT);
    digitalWrite(RFM_RST, HIGH);
    
    if (!rf95.init()) { 
        Serial.println("RFM95 init failed!"); 
        while (1); 
    }

    // Set Modulation Parameters
    rf95.setFrequency(RF_FREQUENCY);
    rf95.setTxPower(TX_OUTPUT_POWER, false);
    rf95.setSignalBandwidth(LORA_BANDWIDTH);
    rf95.setSpreadingFactor(LORA_SPREADING_FACTOR);
    rf95.setCodingRate4(LORA_CODINGRATE);
    rf95.setPreambleLength(LORA_PREAMBLE_LENGTH);
    rf95.setLowDatarate(); 

    // Sync Word 0x2B for Meshtastic compatibility
    spiWriteReg(0x39, 0x2B);
    Serial.println("System Online. Sending SUID + Dual Temp...");
}

void loop() {
    // 1. Read External Physical Sensor (TMP36)
    analogSetPinAttenuation(ADC_PIN, ADC_6db); 
    delay(50); 
    int rawADC = analogRead(ADC_PIN);
    float vExt = (rawADC * 2.2) / 4096.0;
    float extTempC = ((vExt * 1000.0) - 500.0) / 10.0;

    // 2. Read Internal ESP32 Sensor
    float intTempC = (temprature_sens_read() - 32) / 1.8f;

    // 3. Build Packet
    const char* suid = "yimingt";
    size_t txLen = 0;
    auto append8    = [&](uint8_t v)  { txpacket[txLen++] = v; };
    auto append32le = [&](uint32_t v) { for (int i = 0; i < 4; i++) append8((v >> (8*i)) & 0xFF); };

    // --- Meshtastic Header (16 bytes) ---
    append32le(0xFFFFFFFF);           // Destination (Broadcast)
    memcpy(txpacket + 4, suid, 4);    // Source (first 4 bytes of SUID: 'yimi')
    txLen += 4;
    append32le(0x00000001);           // Packet ID
    append8(0x63);                    // Flags (Hops/Limit)
    append8(0x08);                    // Channel Hash
    append8(0x00);                    // Next-Hop
    append8(0x00);                    // Last-Hop

    // --- Payload: SUID + BOTH TEMPS ---
    char msg[100];
    snprintf(msg, sizeof(msg), "User:%s | INT:%.1fC | EXT:%.1fC", suid, intTempC, extTempC);
    
    // Copy message into packet starting after the 16-byte header
    for(int i=0; msg[i] != '\0'; i++) append8(msg[i]);

    // 4. Transmit
    Serial.printf("\n[TX] Sending Packet: %s\n", msg);
    rf95.send(txpacket, (uint8_t)txLen);

    if (rf95.waitPacketSent(2000)) {
        Serial.println("Sent successfully. Listening for 5s...");
        
        // 5. Receive & Print Logic
        uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
        uint8_t len = sizeof(buf);
        if (rf95.waitAvailableTimeout(5000)) { 
            if (rf95.recv(buf, &len)) {
                buf[len] = '\0'; // Safety null-terminate
                
                Serial.printf("[RX] Length: %d | RSSI: %d\n", len, rf95.lastRssi());
                if (len > 16) {
                    // Skips header to display the full string including SUID and temps
                    Serial.print("Repeater Data: ");
                    Serial.println((char*)buf + 16); 
                }
            }
        } else {
            Serial.println("[RX] No response from other nodes.");
        }
    } else {
        Serial.println("[TX] Transmit failed.");
    }

    delay(10000); // 10-second heartbeat
}