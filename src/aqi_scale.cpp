#include "aqi_scale.h"
#include <lvgl.h>
#include <string.h>

// US EPA AQI bands. Colours follow the EPA scheme but brightened slightly so
// purple/maroon remain readable on a black background.
static const AqiBand kBands[] = {
  {0, 50, 0x3DDC84, "Good", "Good",
   "Air is clean. A good time to open windows and be outside.", LV_SYMBOL_OK},
  {51, 100, 0xF7D038, "Moderate", "Moderate",
   "Acceptable. Unusually sensitive people should limit long outdoor exertion.", LV_SYMBOL_OK},
  {101, 150, 0xFF8C1A, "Unhealthy (SG)", "Unhealthy for Sensitive Groups",
   "Children, elderly and people with asthma should cut outdoor exertion. Consider closing windows.",
   LV_SYMBOL_WARNING},
  {151, 200, 0xF2384A, "Unhealthy", "Unhealthy",
   "Limit time outdoors and wear an N95 outside. Keep windows shut and run a purifier.",
   LV_SYMBOL_WARNING},
  {201, 300, 0xB15CE0, "Very Unhealthy", "Very Unhealthy",
   "Avoid outdoor exertion. N95 outdoors, purifier on high indoors.", LV_SYMBOL_WARNING},
  {301, 500, 0xD81B60, "Hazardous", "Hazardous",
   "Stay indoors with windows shut and purifier on. N95 if you must go out.", LV_SYMBOL_WARNING},
  {501, 99999, 0xFF1744, "Hazardous+", "Beyond the AQI scale",
   "Extreme pollution. Stay indoors, seal gaps, run purifiers, N95 for any outdoor trip.",
   LV_SYMBOL_WARNING},
};

int aqi_band_index(int aqi) {
  if (aqi < 0) return 0;
  for (int i = 0; i < (int)(sizeof(kBands) / sizeof(kBands[0])); i++) {
    if (aqi <= kBands[i].hi) return i;
  }
  return 6;
}

const AqiBand& aqi_band(int aqi) { return kBands[aqi_band_index(aqi)]; }

static const char* kLabels[POLL_COUNT] = {"PM2.5", "PM10", "O3", "NO2", "SO2", "CO"};
static const char* kKeys[POLL_COUNT] = {"pm25", "pm10", "o3", "no2", "so2", "co"};

const char* pollutant_label(uint8_t p) { return p < POLL_COUNT ? kLabels[p] : "?"; }
const char* pollutant_key(uint8_t p) { return p < POLL_COUNT ? kKeys[p] : "?"; }

const char* dominant_label(const char* key) {
  if (!key || !key[0]) return "";
  for (int i = 0; i < POLL_COUNT; i++)
    if (strcmp(key, kKeys[i]) == 0) return kLabels[i];
  return key;
}
