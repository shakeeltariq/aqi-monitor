#pragma once
#include <stdint.h>

// User settings, persisted in NVS. Thread-safe copy-in / copy-out access.
struct Settings {
  char waqiToken[64];     // from https://aqicn.org/data-platform/token/
  char station[24];       // "" = nearest to lat/lon, or "@<uid>" for a fixed WAQI station
  float lat;
  float lon;
  uint16_t refreshMin;    // 5..120

  uint8_t brightness;       // 10..255
  uint8_t nightBrightness;  // 0..255 (0 = backlight off at night)
  int8_t nightStart;        // hour 0..23, -1 = night mode off
  int8_t nightEnd;          // hour 0..23
  uint16_t screenTimeoutMin;  // 0 = always on

  char tz[40];            // POSIX TZ string, e.g. "PKT-5"

  char mqttHost[64];      // "" = MQTT disabled
  uint16_t mqttPort;
  char mqttUser[32];
  char mqttPass[64];
};

void settings_begin();
Settings settings_get();
void settings_set(const Settings& s, bool persist = true);
