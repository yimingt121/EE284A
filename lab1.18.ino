// ESP32 LED Controller via Serial
// LEDs connected: GPIO12 (LED1), GPIO13 (LED2), GPIO14 (LED3)
// Each LED in series with 330Ω resistor, GPIO → Resistor → LED → GND

const int LED_PINS[3] = {12, 13, 14};

void setup() {
  Serial.begin(115200);
  
  // Initialize all LED pins as outputs, start LOW (off)
  for (int i = 0; i < 3; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }

  Serial.println("=== ESP32 LED Controller ===");
  Serial.println("Enter a 3-bit code to control LEDs:");
  Serial.println("  Bit 1 = LED1 (GPIO12)");
  Serial.println("  Bit 2 = LED2 (GPIO13)");
  Serial.println("  Bit 3 = LED3 (GPIO14)");
  Serial.println("Example: '101' turns on LED1 and LED3");
  Serial.println("----------------------------");
  Serial.print("Enter code: ");
}

void loop() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();  // Remove whitespace/newline characters

    // Validate input
    if (input.length() != 3) {
      Serial.println("\n[ERROR] Please enter exactly 3 bits (e.g., 101)");
      Serial.print("Enter code: ");
      return;
    }

    bool valid = true;
    for (int i = 0; i < 3; i++) {
      if (input[i] != '0' && input[i] != '1') {
        valid = false;
        break;
      }
    }

    if (!valid) {
      Serial.println("\n[ERROR] Only '0' and '1' are valid characters");
      Serial.print("Enter code: ");
      return;
    }

    // Apply LED states
    Serial.println();
    Serial.print("Setting LEDs: ");
    for (int i = 0; i < 3; i++) {
      bool state = (input[i] == '1');
      digitalWrite(LED_PINS[i], state ? HIGH : LOW);
      Serial.print("LED");
      Serial.print(i + 1);
      Serial.print("=");
      Serial.print(state ? "ON" : "OFF");
      if (i < 2) Serial.print("  ");
    }
    Serial.println();
    Serial.print("Enter code: ");
  }
}
