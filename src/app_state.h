#pragma once
#include <stdint.h>
#include <time.h>
#include <math.h>

// Pollutant order used everywhere (UI ring, MQTT, parsing)
enum Pollutant : uint8_t { POL_PM25 = 0, POL_PM10, POL_O3, POL_NO2, POL_SO2, POL_CO, POLL_COUNT };

enum class DataSource : uint8_t { None = 0, WAQI, OpenMeteo };

enum class NetPhase : uint8_t { Booting = 0, SetupPortal, Connecting, Online, Offline };

static constexpr int TREND_POINTS = 24;   // 12 h past + 12 h ahead
static constexpr int TREND_NOW_IDX = 12;

struct AqiData {
  bool valid = false;
  int aqi = -1;                        // US EPA AQI (can exceed 500)
  char dominant[8] = "";               // "pm25", "pm10", "o3", ...
  char station[64] = "";
  int16_t sub[POLL_COUNT] = {-1, -1, -1, -1, -1, -1};  // per-pollutant AQI sub-index, -1 = n/a
  float tempC = NAN;
  float humidity = NAN;
  time_t observed = 0;                 // UTC epoch of the measurement / model hour
  time_t fetched = 0;
  DataSource source = DataSource::None;

  bool trendValid = false;
  int16_t trend[TREND_POINTS];         // -1 = missing
  int tomorrowPeak = -1;
};

// Snapshot handed from the network task to the UI task through a 1-slot queue.
struct AppState {
  uint32_t seq = 0;
  NetPhase phase = NetPhase::Booting;
  bool fetching = false;
  int8_t otaProgress = -1;             // 0..100 while updating, -1 otherwise
  char ip[16] = "";
  char apName[32] = "";
  char lastError[64] = "";
  AqiData data;
};
