#include "settings.h"
#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

static Settings s_cur;
static SemaphoreHandle_t s_mx = nullptr;
static const char* NS = "aqimon";

static void defaults(Settings& s) {
  memset(&s, 0, sizeof(s));
  s.lat = 31.5204f;   // Lahore city centre; set your exact location in the web setup
  s.lon = 74.3587f;
  s.refreshMin = 10;
  s.brightness = 200;
  s.nightBrightness = 30;
  s.nightStart = 23;
  s.nightEnd = 7;
  s.screenTimeoutMin = 0;
  strlcpy(s.tz, "PKT-5", sizeof(s.tz));
  s.mqttPort = 1883;
}

static void loadStr(Preferences& p, const char* key, char* buf, size_t len) {
  if (p.isKey(key)) p.getString(key, buf, len);
}

void settings_begin() {
  s_mx = xSemaphoreCreateMutex();
  defaults(s_cur);

  Preferences p;
  if (!p.begin(NS, true)) return;  // first boot: namespace does not exist yet
  loadStr(p, "tok", s_cur.waqiToken, sizeof(s_cur.waqiToken));
  loadStr(p, "stn", s_cur.station, sizeof(s_cur.station));
  s_cur.lat = p.getFloat("lat", s_cur.lat);
  s_cur.lon = p.getFloat("lon", s_cur.lon);
  s_cur.refreshMin = p.getUShort("ref", s_cur.refreshMin);
  s_cur.brightness = p.getUChar("bri", s_cur.brightness);
  s_cur.nightBrightness = p.getUChar("nbri", s_cur.nightBrightness);
  s_cur.nightStart = p.getChar("nst", s_cur.nightStart);
  s_cur.nightEnd = p.getChar("nen", s_cur.nightEnd);
  s_cur.screenTimeoutMin = p.getUShort("sto", s_cur.screenTimeoutMin);
  loadStr(p, "tz", s_cur.tz, sizeof(s_cur.tz));
  loadStr(p, "mh", s_cur.mqttHost, sizeof(s_cur.mqttHost));
  s_cur.mqttPort = p.getUShort("mp", s_cur.mqttPort);
  loadStr(p, "mu", s_cur.mqttUser, sizeof(s_cur.mqttUser));
  loadStr(p, "mpw", s_cur.mqttPass, sizeof(s_cur.mqttPass));
  p.end();
}

Settings settings_get() {
  xSemaphoreTake(s_mx, portMAX_DELAY);
  Settings copy = s_cur;
  xSemaphoreGive(s_mx);
  return copy;
}

void settings_set(const Settings& s, bool persist) {
  xSemaphoreTake(s_mx, portMAX_DELAY);
  s_cur = s;
  if (persist) {
    Preferences p;
    if (p.begin(NS, false)) {
      p.putString("tok", s.waqiToken);
      p.putString("stn", s.station);
      p.putFloat("lat", s.lat);
      p.putFloat("lon", s.lon);
      p.putUShort("ref", s.refreshMin);
      p.putUChar("bri", s.brightness);
      p.putUChar("nbri", s.nightBrightness);
      p.putChar("nst", s.nightStart);
      p.putChar("nen", s.nightEnd);
      p.putUShort("sto", s.screenTimeoutMin);
      p.putString("tz", s.tz);
      p.putString("mh", s.mqttHost);
      p.putUShort("mp", s.mqttPort);
      p.putString("mu", s.mqttUser);
      p.putString("mpw", s.mqttPass);
      p.end();
    }
  }
  xSemaphoreGive(s_mx);
}
