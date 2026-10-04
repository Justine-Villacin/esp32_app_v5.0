/*
  SmartGrow_ESP32.ino
  ESP32 Greenhouse Monitor — production firmware for the SmartGrow dashboard.

  WHAT THIS DOES
  ---------------
  - Reads temperature/humidity (DHT22), soil moisture (capacitive analog),
    and ambient light (LDR) on a fixed interval.
  - Runs automation LOCALLY, on the ESP32 itself. The pump, grow light, and
    exhaust fan are controlled by THIS firmware — the website only ever
    displays what this firmware reports; it does not remotely command the
    relays.
  - FAIL-SAFE: the instant a sensor looks disconnected, the relay(s) that
    depend on it are forced OFF immediately, every loop — not just reported
    as off. This is the hardware-level fix for "relays stay on when sensors
    disconnect": DHT failure -> fan off, bad soil reading -> pump off, bad
    light reading -> grow light off.
  - Reports every reading to the SmartGrow backend (local desktop mode over
    HTTP, or a hosted Vercel deployment over HTTPS) on a fixed interval,
    through a WiFi connection that reconnects itself in the background
    without ever blocking the sensor/automation loop (fixes "constant
    disconnection of ESP32" caused by a previous blocking reconnect).

  JSON PAYLOAD SENT TO POST /update (keep this in sync with app.py):
    {
      "temperature": 24.5,          // omitted if DHT read failed
      "humidity": 61.0,             // omitted if DHT read failed
      "soil_moisture": 42,          // omitted if soil sensor looks disconnected
      "light": 480,                 // 0-1000, omitted if LDR looks disconnected
      "dht_connected": true,
      "soil_connected": true,
      "light_connected": true,
      "fan": "OFF",                 // relay state
      "water_pump": "OFF",          // relay state
      "grow_light": "ON"            // relay state — NOT the same field as "light" above
    }
  "light" (the numeric LDR reading) and "grow_light" (the relay command) are
  deliberately two different keys. An earlier version of this project used
  "light" for both, which silently broke the numeric reading — see
  CHANGELOG.md.

  SETUP CHECKLIST
  -----------------
  1. Install libraries (Arduino IDE -> Tools -> Manage Libraries):
       - "DHT sensor library" by Adafruit (pulls in "Adafruit Unified
         Sensor" automatically)
       - "ArduinoJson" by Benoit Blanchon — version 7.x
  2. Open config.h (the second tab next to this file) and fill in your
     WiFi credentials, server address, and API key.
  3. Run SmartGrow_Diagnostics.ino FIRST on the same wiring to confirm every
     sensor/relay works and to find your calibration numbers — then put
     those numbers in config.h.
  4. Flash this sketch. Open Tools -> Serial Monitor at 115200 baud to watch
     it run.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include "config.h"

DHT dht(DHT_PIN, DHT_TYPE);

// ---- Relay helper: abstracts active-HIGH vs active-LOW relay boards ----
void setRelay(uint8_t pin, bool on) {
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(pin, level ? HIGH : LOW);
}

// ---- Live state ----
float temperatureC = 0.0f;
float humidityPct = 0.0f;
int soilPct = 0;
int lightLevel = 0;

bool dhtConnected = false;
bool soilConnected = false;
bool lightConnected = false;

bool pumpOn = false;
bool growLightOn = false;
bool fanOn = false;

int soilRailCount = 0;   // consecutive rail-pinned (0 or 4095) readings
int ldrRailCount = 0;

unsigned long lastSensorRead = 0;
unsigned long lastServerPost = 0;
unsigned long lastWifiAttempt = 0;

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[SmartGrow] Booting...");

  pinMode(RELAY_PUMP_PIN, OUTPUT);
  pinMode(RELAY_LIGHT_PIN, OUTPUT);
  pinMode(RELAY_FAN_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);

  // Never boot with actuators on based on stale/garbage state — everything
  // starts OFF until a real sensor reading says otherwise.
  setRelay(RELAY_PUMP_PIN, false);
  setRelay(RELAY_LIGHT_PIN, false);
  setRelay(RELAY_FAN_PIN, false);

  dht.begin();
  connectWiFi();
}

// ---------------------------------------------------------------------------
void loop() {
  unsigned long now = millis();

  maintainWiFi(now);

  if (now - lastSensorRead >= SENSOR_READ_INTERVAL_MS) {
    lastSensorRead = now;
    readSensors();
    runAutomation();
    printStatus();
  }

  if (now - lastServerPost >= SERVER_POST_INTERVAL_MS) {
    lastServerPost = now;
    if (WiFi.status() == WL_CONNECTED) {
      postReading();
    } else {
      Serial.println("[SmartGrow] Skipping post — WiFi not connected.");
    }
  }

  // Status LED: solid while WiFi is connected, slow blink while it isn't.
  digitalWrite(STATUS_LED_PIN, WiFi.status() == WL_CONNECTED ? HIGH : ((now / 300) % 2));
}

// ---------------------------------------------------------------------------
// WiFi — non-blocking connect + reconnect. A flaky network never freezes
// the sensor/automation loop, and we never get stuck retrying forever
// inside setup(). This is the fix for "constant disconnection of ESP32"
// when the original blocking reconnect logic stalled the whole device.
// ---------------------------------------------------------------------------
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[SmartGrow] Connecting to WiFi");

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[SmartGrow] WiFi connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[SmartGrow] Not connected yet — will keep retrying in the background.");
  }
}

void maintainWiFi(unsigned long now) {
  if (WiFi.status() == WL_CONNECTED) return;
  if (now - lastWifiAttempt < WIFI_RECONNECT_INTERVAL_MS) return;

  lastWifiAttempt = now;
  Serial.println("[SmartGrow] WiFi down — reconnecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// ---------------------------------------------------------------------------
// Sensors
// ---------------------------------------------------------------------------
void readSensors() {
  // --- DHT22 ---
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) {
    dhtConnected = false;
    // Deliberately NOT zeroing temperatureC/humidityPct here — the
    // dashboard already ignores them once dhtConnected is false, this just
    // avoids a cosmetic flash to 0.0 on a single missed read.
  } else {
    dhtConnected = true;
    temperatureC = t;
    humidityPct = h;
  }

  // --- Soil moisture (capacitive, analog) ---
  // A raw reading pinned at a rail for several reads in a row is the
  // real-world signature of an unplugged/shorted sensor, as opposed to one
  // noisy sample.
  int soilRaw = analogRead(SOIL_PIN);
  if (soilRaw <= 5 || soilRaw >= 4090) {
    soilRailCount++;
  } else {
    soilRailCount = 0;
  }
  soilConnected = soilRailCount < RAIL_COUNT_THRESHOLD;
  if (soilConnected) {
    long pct = map(soilRaw, SOIL_RAW_DRY, SOIL_RAW_WET, 0, 100);
    soilPct = constrain(pct, 0, 100);
  }

  // --- Light (LDR, analog) ---
  int ldrRaw = analogRead(LDR_PIN);
  if (ldrRaw <= 5 || ldrRaw >= 4090) {
    ldrRailCount++;
  } else {
    ldrRailCount = 0;
  }
  lightConnected = ldrRailCount < RAIL_COUNT_THRESHOLD;
  if (lightConnected) {
    long level = map(ldrRaw, LDR_RAW_DARK, LDR_RAW_BRIGHT, 0, 1000);
    lightLevel = constrain(level, 0, 1000);
  }
}

// ---------------------------------------------------------------------------
// Automation — runs locally, independent of WiFi/the dashboard being
// reachable at all. Every branch enforces: sensor not connected -> actuator
// forced OFF, checked fresh every single loop (not just on the transition),
// so a relay can never be left stuck on from a reading taken before the
// sensor failed. Hysteresis (separate ON/OFF thresholds) stops relays from
// chattering rapidly when a reading sits right at the edge of a threshold.
// ---------------------------------------------------------------------------
void runAutomation() {
  // Exhaust fan <- temperature
  if (!dhtConnected) {
    fanOn = false;
  } else if (temperatureC >= TEMP_FAN_ON_C) {
    fanOn = true;
  } else if (temperatureC <= TEMP_FAN_OFF_C) {
    fanOn = false;
  } // else: inside the hysteresis band — leave fanOn as it was

  // Water pump <- soil moisture
  if (!soilConnected) {
    pumpOn = false;
  } else if (soilPct <= SOIL_PUMP_ON_PCT) {
    pumpOn = true;
  } else if (soilPct >= SOIL_PUMP_OFF_PCT) {
    pumpOn = false;
  }

  // Grow light <- ambient light
  if (!lightConnected) {
    growLightOn = false;
  } else if (lightLevel < LIGHT_GROWLIGHT_ON) {
    growLightOn = true;
  } else if (lightLevel >= LIGHT_GROWLIGHT_OFF) {
    growLightOn = false;
  }

  setRelay(RELAY_FAN_PIN, fanOn);
  setRelay(RELAY_PUMP_PIN, pumpOn);
  setRelay(RELAY_LIGHT_PIN, growLightOn);
}

// ---------------------------------------------------------------------------
// Reporting to the dashboard
// ---------------------------------------------------------------------------
void postReading() {
  JsonDocument doc;   // ArduinoJson v7. On v6, use StaticJsonDocument<384> doc; instead.

  if (dhtConnected) {
    doc["temperature"] = temperatureC;
    doc["humidity"] = humidityPct;
  }
  if (soilConnected) {
    doc["soil_moisture"] = soilPct;
  }
  if (lightConnected) {
    doc["light"] = lightLevel;
  }
  doc["dht_connected"] = dhtConnected;
  doc["soil_connected"] = soilConnected;
  doc["light_connected"] = lightConnected;
  doc["fan"] = fanOn ? "ON" : "OFF";
  doc["water_pump"] = pumpOn ? "ON" : "OFF";
  doc["grow_light"] = growLightOn ? "ON" : "OFF";

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  String url;
  bool began;

  // IMPORTANT: a fresh WiFiClientSecure is created on every single call,
  // not reused across calls (it used to be `static`). A single
  // WiFiClientSecure reused for repeated HTTPS requests on ESP32 commonly
  // works for the first request or two and then starts failing with
  // "connection refused" as its internal TLS session state gets stale —
  // this was the actual cause of the POST failures. A plain local variable
  // here means a clean TLS connection every time, at the cost of a few
  // hundred ms more per post for the handshake, which is a fine trade-off
  // at a 2-second post interval.
  WiFiClientSecure secureClient;

  if (SERVER_USE_HTTPS) {
    // Skips certificate validation — the simplest working option for a
    // hobbyist device without managing a CA bundle. If you need real
    // certificate validation, replace this with secureClient.setCACert(...)
    // and your host's root CA. MUST be set before any connect() attempt,
    // including the diagnostic one just below — otherwise the diagnostic
    // connect would try (and fail) full certificate-chain validation,
    // reporting a misleading failure unrelated to the real POST attempt.
    secureClient.setInsecure();

    // --- Diagnostics: isolate DNS vs. TCP vs. TLS before trying HTTPClient ---
    // "connection refused" from HTTPClient is a catch-all error code that
    // can mean a DNS failure, a blocked/unreachable TCP connection, or a
    // failed TLS handshake. These lines print exactly which layer fails.
    IPAddress resolvedIP;
    if (WiFi.hostByName(SERVER_HOST, resolvedIP)) {
      Serial.printf("[SmartGrow][diag] DNS OK: %s -> %s\n", SERVER_HOST, resolvedIP.toString().c_str());
    } else {
      Serial.printf("[SmartGrow][diag] DNS FAILED for %s (network/router may be blocking or slow DNS)\n", SERVER_HOST);
    }

    bool rawConnected = secureClient.connect(SERVER_HOST, 443);
    Serial.printf("[SmartGrow][diag] Raw TLS connect() to %s:443 -> %s\n", SERVER_HOST, rawConnected ? "SUCCESS" : "FAILED");
    if (!rawConnected) {
      char errBuf[160];
      secureClient.lastError(errBuf, sizeof(errBuf));
      Serial.printf("[SmartGrow][diag] mbedTLS error detail: %s\n", errBuf);
    } else {
      secureClient.stop(); // diagnostic connection only; HTTPClient opens its own fresh one below
    }
    // --- End diagnostics ---

    url = String("https://") + SERVER_HOST + UPDATE_PATH;
    began = http.begin(secureClient, url);
  } else {
    url = String("http://") + SERVER_HOST + ":" + String(SERVER_PORT) + UPDATE_PATH;
    began = http.begin(url);
  }

  if (!began) {
    Serial.println("[SmartGrow] HTTPClient.begin() failed — check SERVER_HOST/PORT in config.h.");
    return;
  }

  http.addHeader("Content-Type", "application/json");
  if (strlen(API_KEY) > 0) {
    http.addHeader("X-API-Key", API_KEY);
  }
  http.setTimeout(5000);
  http.setConnectTimeout(5000);

  Serial.printf("[SmartGrow] Free heap before POST: %u bytes\n", ESP.getFreeHeap());

  int status = http.POST(payload);
  if (status > 0) {
    if (status == 200) {
      Serial.println("[SmartGrow] POST -> HTTP 200 OK");
    } else {
      Serial.printf("[SmartGrow] POST -> HTTP %d: %s\n", status, http.getString().c_str());
      if (status == 401) {
        Serial.println("[SmartGrow] 401 Unauthorized — API_KEY in config.h doesn't match SMARTGROW_API_KEY on the server.");
      }
    }
  } else {
    Serial.printf("[SmartGrow] POST failed: %s (check SERVER_HOST/PORT/HTTPS setting)\n", http.errorToString(status).c_str());
  }

  http.end();
  secureClient.stop(); // release the TLS socket promptly instead of waiting on it to be garbage-collected
}

// ---------------------------------------------------------------------------
void printStatus() {
  Serial.println("-------------------------------------------");
  Serial.printf("Temp: %s  Hum: %s\n",
                dhtConnected ? (String(temperatureC, 1) + "C").c_str() : "OFFLINE",
                dhtConnected ? (String(humidityPct, 0) + "%").c_str() : "OFFLINE");
  Serial.printf("Soil: %s  Light: %s\n",
                soilConnected ? (String(soilPct) + "%").c_str() : "OFFLINE",
                lightConnected ? String(lightLevel).c_str() : "OFFLINE");
  Serial.printf("Pump: %s  GrowLight: %s  Fan: %s\n",
                pumpOn ? "ON" : "off", growLightOn ? "ON" : "off", fanOn ? "ON" : "off");
  Serial.printf("WiFi: %s\n",
                WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "disconnected");
}
