// config.h — SmartGrow ESP32 firmware configuration.
// Edit the values below, then flash SmartGrow_ESP32.ino.
// (In the Arduino IDE this appears as a second tab next to the .ino file —
// you don't need to do anything special to "include" it, just fill it in.)

#ifndef SMARTGROW_CONFIG_H
#define SMARTGROW_CONFIG_H

// ============================================================
// WiFi
// ============================================================
#define WIFI_SSID        "BFDC_SKY_4G"
#define WIFI_PASSWORD    "VILLACIN!#00"

// ============================================================
// Server — pick ONE target
// ============================================================
// Currently set to your Vercel deployment (HTTPS). If you ever want to
// point this board at your PC's local desktop mode instead
// (`python desktop.py`), comment the Vercel block below and uncomment the
// local one — your PC's LAN IP is already filled in as 192.168.1.10.
//
// --- Local desktop mode ---
//     #define SERVER_USE_HTTPS  false
//     #define SERVER_HOST       "192.168.1.10"
//     #define SERVER_PORT       8000
//
// --- Hosted on Vercel (active) ---
#define SERVER_USE_HTTPS   false
#define SERVER_HOST        "192.168.1.54"
#define SERVER_PORT        8000   // unused while SERVER_USE_HTTPS is true, but must stay defined
#define UPDATE_PATH        "/update"

// Must exactly match the SMARTGROW_API_KEY environment variable set on the
// server (see README.md / .env.example). Leave as "" ONLY while
// SMARTGROW_API_KEY is unset server-side — otherwise every post will get a
// 401 and the dashboard will look permanently disconnected.
#define API_KEY             ""

// ============================================================
// Pins (ESP32 DevKit v1 / similar 30-38 pin boards)
// ============================================================
#define DHT_PIN              14     // DHT data pin
#define DHT_TYPE              DHT11  // change to DHT22 if you swap sensors later

#define SOIL_PIN              34   // capacitive soil moisture sensor (analog, ADC1-capable pin)
#define LDR_PIN                35   // LDR light sensor (analog, ADC1-capable pin)

#define RELAY_PUMP_PIN         25   // water pump relay
#define RELAY_LIGHT_PIN        26   // grow light relay
#define RELAY_FAN_PIN           27   // exhaust fan relay

#define STATUS_LED_PIN           2   // onboard LED on most ESP32 DevKit boards

// Most inexpensive relay modules switch ON when the control pin is driven
// LOW ("active-low"). If yours turns ON when driven HIGH instead, set this
// to false.
#define RELAY_ACTIVE_LOW     true

// ============================================================
// Calibration — REQUIRED before trusting the readings
// ============================================================
// Run SmartGrow_Diagnostics.ino first (same wiring, no WiFi required) and
// watch the raw ADC numbers (0-4095) it prints on the Serial Monitor:
//
//   Soil sensor: note the raw value with the probe completely dry/in open
//   air, and the raw value with the probe fully submerged in water.
#define SOIL_RAW_DRY          3000   // raw ADC reading in dry air  -> reported as 0%
#define SOIL_RAW_WET          1200   // raw ADC reading submerged   -> reported as 100%

//   LDR: note the raw value with it fully covered/dark, and the raw value
//   under bright light (a phone flashlight a few cm away works well).
#define LDR_RAW_DARK           4000   // raw ADC reading, dark   -> reported as 0
#define LDR_RAW_BRIGHT          200   // raw ADC reading, bright  -> reported as 1000

// ============================================================
// Automation thresholds
// ============================================================
// These MUST match the dashboard's thresholds (public/js/ui.js and the
// labels in templates/index.html) so what the website displays and what
// the hardware actually does always agree. If you change one side, change
// the other too.
#define TEMP_FAN_ON_C          30.0   // fan turns ON at/above this
#define TEMP_FAN_OFF_C         28.0   // fan turns OFF at/below this (hysteresis gap avoids relay chatter)

#define SOIL_PUMP_ON_PCT        40    // pump turns ON at/below this
#define SOIL_PUMP_OFF_PCT       45    // pump turns OFF at/above this

#define LIGHT_GROWLIGHT_ON      300   // grow light turns ON below this
#define LIGHT_GROWLIGHT_OFF     320   // grow light turns OFF at/above this

// ============================================================
// Timing (milliseconds)
// ============================================================
#define SENSOR_READ_INTERVAL_MS     2000   // how often sensors are read & automation is evaluated
#define SERVER_POST_INTERVAL_MS     2000   // how often a reading is sent to the dashboard
#define WIFI_RECONNECT_INTERVAL_MS  5000   // how often to retry WiFi while disconnected

// A sensor's raw ADC reading pinned at a rail (near 0 or near 4095) for
// this many consecutive reads is treated as "disconnected" rather than a
// single noisy sample. At SENSOR_READ_INTERVAL_MS=2000, 4 reads = ~8s,
// matching the dashboard's own disconnect timeout.
#define RAIL_COUNT_THRESHOLD         4

#endif // SMARTGROW_CONFIG_H
