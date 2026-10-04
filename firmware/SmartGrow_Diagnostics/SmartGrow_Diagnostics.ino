/*
  SmartGrow_Diagnostics.ino
  Stand-alone wiring + calibration helper — NOT the production firmware.

  Run this FIRST, before SmartGrow_ESP32.ino, to:
    1. Confirm every sensor and every relay is wired to the right pin and
       actually working, one at a time, before anything automated is
       touching them.
    2. Find the raw calibration numbers (SOIL_RAW_DRY/WET, LDR_RAW_DARK/
       BRIGHT) to put into SmartGrow_ESP32/config.h.

  No WiFi, no server, no ArduinoJson — only the DHT library, so this stays
  simple to upload and read even before your network/server side is ready.

  After uploading: Tools -> Serial Monitor, set the baud rate to 115200.

  SERIAL COMMANDS (type into the Serial Monitor's input box, press Enter):
    p1 / p0   - turn the PUMP relay on / off
    l1 / l0   - turn the GROW LIGHT relay on / off
    f1 / f0   - turn the FAN relay on / off
  Sensor readings print automatically every 2 seconds regardless.
*/

#include <DHT.h>

// Keep these in sync with SmartGrow_ESP32/config.h if you change your wiring.
#define DHT_PIN          4
#define DHT_TYPE         DHT22
#define SOIL_PIN         34
#define LDR_PIN          35
#define RELAY_PUMP_PIN   25
#define RELAY_LIGHT_PIN  26
#define RELAY_FAN_PIN    27
#define RELAY_ACTIVE_LOW true

DHT dht(DHT_PIN, DHT_TYPE);
unsigned long lastRead = 0;

void setRelay(uint8_t pin, bool on) {
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(pin, level ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(RELAY_PUMP_PIN, OUTPUT);
  pinMode(RELAY_LIGHT_PIN, OUTPUT);
  pinMode(RELAY_FAN_PIN, OUTPUT);
  setRelay(RELAY_PUMP_PIN, false);
  setRelay(RELAY_LIGHT_PIN, false);
  setRelay(RELAY_FAN_PIN, false);

  dht.begin();

  Serial.println("\n=== SmartGrow Diagnostics ===");
  Serial.println("Commands: p1/p0 (pump), l1/l0 (grow light), f1/f0 (fan)");
  Serial.println("Sensor readings print automatically every 2 seconds.\n");
}

void loop() {
  handleSerialCommands();

  if (millis() - lastRead >= 2000) {
    lastRead = millis();
    printReadings();
  }
}

void handleSerialCommands() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd == "p1") { setRelay(RELAY_PUMP_PIN, true);  Serial.println(">> Pump relay -> ON"); }
  else if (cmd == "p0") { setRelay(RELAY_PUMP_PIN, false); Serial.println(">> Pump relay -> OFF"); }
  else if (cmd == "l1") { setRelay(RELAY_LIGHT_PIN, true);  Serial.println(">> Grow light relay -> ON"); }
  else if (cmd == "l0") { setRelay(RELAY_LIGHT_PIN, false); Serial.println(">> Grow light relay -> OFF"); }
  else if (cmd == "f1") { setRelay(RELAY_FAN_PIN, true);  Serial.println(">> Fan relay -> ON"); }
  else if (cmd == "f0") { setRelay(RELAY_FAN_PIN, false); Serial.println(">> Fan relay -> OFF"); }
  else if (cmd.length() > 0) { Serial.println("Unknown command. Use p1/p0, l1/l0, f1/f0."); }
}

void printReadings() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int soilRaw = analogRead(SOIL_PIN);
  int ldrRaw = analogRead(LDR_PIN);

  Serial.println("-------------------------------------------");
  if (isnan(h) || isnan(t)) {
    Serial.println("DHT22:  READ FAILED — check wiring/power/data pin.");
  } else {
    Serial.printf("DHT22:  Temp=%.1fC  Humidity=%.0f%%\n", t, h);
  }
  Serial.printf("Soil (raw ADC, 0-4095): %d   <- note the DRY and WET values for config.h\n", soilRaw);
  Serial.printf("LDR  (raw ADC, 0-4095): %d   <- note the DARK and BRIGHT values for config.h\n", ldrRaw);
}
