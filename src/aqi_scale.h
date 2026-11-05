#pragma once
#include <stdint.h>
#include "app_state.h"

struct AqiBand {
  int lo, hi;
  uint32_t color;          // 0xRRGGBB, tuned for a dark screen
  const char* shortName;   // fits the home screen
  const char* longName;
  const char* tip;
  const char* icon;        // LV_SYMBOL_* string
};

int aqi_band_index(int aqi);          // 0..6
const AqiBand& aqi_band(int aqi);

const char* pollutant_label(uint8_t p);   // "PM2.5"
const char* pollutant_key(uint8_t p);     // "pm25" (WAQI / MQTT key)
const char* dominant_label(const char* waqiKey);  // "pm25" -> "PM2.5"
