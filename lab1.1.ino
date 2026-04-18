#define ADC_PIN A2 // Using Pin A2 as requested

void setup() {
  Serial.begin(115200);
  
  // Set ADC to 12-bit resolution as requested
  analogReadResolution(12); 
}

void loop() {
  // Set attenuation to 6 dB at the beginning of the loop
  analogSetPinAttenuation(ADC_PIN, ADC_6db); 
  delay(50); // Stabilization delay

  // Read the raw ADC value
  int rawADC = analogRead(ADC_PIN);

  // Calculate voltage in Volts (V)
  // 3.3V reference / 4096 (12-bit)
  float voltage = (rawADC * 2.2) / 4096.0;

  // Calculate temperature in Celsius
  // (Voltage in mV - 500mV offset) / 10mV per degree
  float tempC = ( (voltage * 1000.0) - 500.0 ) / 10.0;

  // Print the required string
  Serial.print("The raw ADC value is ");
  Serial.print(rawADC);
  Serial.print(", which converts to ");
  Serial.print(voltage);
  Serial.print("V. The temperature is ");
  
  // Threshold logic for 23C
  if (tempC > 23.0) {
    Serial.print("above");
  } else {
    Serial.print("below");
  }
  
  Serial.print(" 23C, in fact it is ");
  Serial.print(tempC);
  Serial.println("C");

  delay(1000); // Wait 1 second between reads
}