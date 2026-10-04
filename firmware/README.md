# SmartGrow Firmware

Two sketches:

| Sketch | Purpose |
|---|---|
| `SmartGrow_Diagnostics/SmartGrow_Diagnostics.ino` | Run this **first**. No WiFi needed. Confirms every sensor/relay is wired correctly and gives you the raw numbers to calibrate with. |
| `SmartGrow_ESP32/SmartGrow_ESP32.ino` (+ `config.h`) | The real firmware: sensors, local automation with fail-safes, WiFi, and reporting to the SmartGrow dashboard. |

## Hardware

- ESP32 DevKit (any board with the standard 30/38-pin DevKit layout)
- DHT22 temperature/humidity sensor
- Capacitive soil moisture sensor (analog output)
- LDR (photoresistor) light sensor, wired as a voltage divider to an analog pin
- A 3-channel relay module (or three single relays) for: water pump, grow light, exhaust fan
- The pump, grow light, and fan themselves, wired through the relays — **follow your relay module's and your devices' own safety instructions, especially for anything on mains voltage.**

## Wiring (matches the pin numbers in `config.h`)

| Component | ESP32 Pin | Notes |
|---|---|---|
| DHT22 data | GPIO 14 | Needs a pull-up resistor (many breakout boards include one already) |
| Soil sensor analog out | GPIO 34 | Input-only ADC pin — fine for an analog read |
| LDR voltage divider | GPIO 35 | Input-only ADC pin |
| Relay — Water Pump | GPIO 25 | |
| Relay — Grow Light | GPIO 26 | |
| Relay — Exhaust Fan | GPIO 27 | |
| Status LED | GPIO 2 | Built-in LED on most DevKit boards — no wiring needed |

If your wiring differs, change the `#define ..._PIN` lines in `config.h` to match — nothing else in the sketch needs to change.

## 1. Install libraries

In the Arduino IDE: **Tools → Manage Libraries**, then install:

- **DHT sensor library** by Adafruit (installing this will also offer to install **Adafruit Unified Sensor** — accept that too)
- **ArduinoJson** by Benoit Blanchon — **version 7.x** (the firmware uses the v7 `JsonDocument` API; on v6 you'd need `StaticJsonDocument<384>` instead — the code has a comment at that line either way)

Install the ESP32 board support itself via **Tools → Board → Boards Manager → "esp32" by Espressif Systems**, if you haven't already.

## 2. Run the diagnostics sketch

1. Open `SmartGrow_Diagnostics/SmartGrow_Diagnostics.ino`, select your ESP32 board and port, upload it.
2. Open **Tools → Serial Monitor**, set the baud rate to **115200**.
3. Confirm temperature/humidity print correctly (not "READ FAILED").
4. Note the soil sensor's raw number with the probe dry, then with it in a cup of water.
5. Note the LDR's raw number covered/dark, then under bright light.
6. Type `p1`, `l1`, `f1` (and `p0`, `l0`, `f0`) into the Serial Monitor to confirm each relay physically clicks and controls the right device.

## 3. Configure and flash the real firmware

1. Open `SmartGrow_ESP32/SmartGrow_ESP32.ino` — `config.h` opens alongside it as a second tab.
2. Fill in `WIFI_SSID` / `WIFI_PASSWORD`.
3. Fill in `SERVER_HOST` / `SERVER_PORT` / `SERVER_USE_HTTPS` for whichever target you're using (local desktop mode or your Vercel deployment — both are documented inline in `config.h`).
4. Fill in `API_KEY` to match `SMARTGROW_API_KEY` on the server, if you've set one.
5. Fill in the calibration numbers you found in step 2.
6. Upload, then reopen the Serial Monitor at 115200 to watch it connect and start posting.

## Why pump/light/fan might not match what you expect

Automation runs **on the ESP32**, not on the website — the dashboard only displays what this firmware reports. If a sensor the automation depends on looks disconnected (DHT read failure, or an analog sensor pinned at 0 or 4095 for several reads in a row), that sensor's actuator is forced OFF immediately, every loop, regardless of what it was doing a moment before. This is checked fresh on every pass through `loop()`, so a relay can never get stuck on from a reading taken before its sensor failed.
