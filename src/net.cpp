// Network task: WiFi provisioning (WiFiManager), NTP, WAQI + Open-Meteo fetch,
// OTA updates and optional MQTT publishing with Home Assistant discovery.
// Runs on core 0. Never touches LVGL; it hands AppState snapshots to the UI.

#include "net.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <math.h>
#include <time.h>

#include "aqi_scale.h"
#include "settings.h"
#include "version.h"

static QueueHandle_t s_q = nullptr;
static TaskHandle_t s_task = nullptr;
static volatile bool s_resetWifi = false;
static AppState s_st;  // owned by the net task

static WiFiManager wm;
static WiFiClient s_mqttNet;
static PubSubClient s_mqtt(s_mqttNet);
static char s_devId[24];
static char s_stateTopic[64];
static char s_availTopic[64];
static bool s_discoverySent = false;
static uint32_t s_mqttLastTry = 0;

// Setup-page parameters (WiFiManager keeps pointers, so they live for the program's lifetime)
static WiFiManagerParameter* p_token;
static WiFiManagerParameter* p_station;
static WiFiManagerParameter* p_lat;
static WiFiManagerParameter* p_lon;
static WiFiManagerParameter* p_refresh;
static WiFiManagerParameter* p_tz;
static WiFiManagerParameter* p_nstart;
static WiFiManagerParameter* p_nend;
static WiFiManagerParameter* p_nbri;
static WiFiManagerParameter* p_sto;
static WiFiManagerParameter* p_mhost;
static WiFiManagerParameter* p_mport;
static WiFiManagerParameter* p_muser;
static WiFiManagerParameter* p_mpass;

static void publish() {
  s_st.seq++;
  xQueueOverwrite(s_q, &s_st);
}

static void setError(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s_st.lastError, sizeof(s_st.lastError), fmt, ap);
  va_end(ap);
  Serial.printf("[net] %s\n", s_st.lastError);
}

// ---------------------------------------------------------------------------
// Time helpers
// ---------------------------------------------------------------------------
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

// Parses "YYYY-MM-DDTHH:MM[:SS][+HH:MM|-HH:MM|Z]" to a UTC epoch.
// `localOffsetSec` is subtracted when the string carries no offset of its own.
static time_t parseIso(const char* s, long localOffsetSec = 0) {
  if (!s) return 0;
  int Y, M, D, h, mi;
  if (sscanf(s, "%d-%d-%dT%d:%d", &Y, &M, &D, &h, &mi) != 5) return 0;
  int sec = 0;
  const char* p = s + 16;
  if (strlen(s) >= 19 && *p == ':') {
    sec = atoi(p + 1);
    p += 3;
  }
  long off = localOffsetSec;
  if (*p == '+' || *p == '-') {
    int oh = 0, om = 0;
    sscanf(p + 1, "%d:%d", &oh, &om);
    off = (oh * 3600L + om * 60L) * (*p == '-' ? -1 : 1);
  } else if (*p == 'Z') {
    off = 0;
  }
  int64_t t = daysFromCivil(Y, M, D) * 86400LL + h * 3600 + mi * 60 + sec - off;
  return (time_t)t;
}

static bool timeValid() { return time(nullptr) > 1700000000; }

// ---------------------------------------------------------------------------
// WAQI (station measurement)
// ---------------------------------------------------------------------------
static bool fetchWaqi(const Settings& s, AqiData& d, String& err) {
  if (!s.waqiToken[0]) {
    err = "no WAQI token";
    return false;
  }
  String url = "https://api.waqi.info/feed/";
  if (s.station[0]) {
    url += s.station;
  } else {
    url += "geo:" + String(s.lat, 4) + ";" + String(s.lon, 4);
  }
  url += "/?token=";
  url += s.waqiToken;

  WiFiClientSecure client;
  client.setInsecure();  // see README: public data, certificate not pinned
  HTTPClient http;
  http.useHTTP10(true);  // avoid chunked encoding so we can stream-parse
  http.setTimeout(15000);
  if (!http.begin(client, url)) {
    err = "WAQI connect failed";
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    err = "WAQI HTTP " + String(code);
    http.end();
    return false;
  }

  JsonDocument filter;
  filter["status"] = true;
  filter["data"]["aqi"] = true;
  filter["data"]["dominentpol"] = true;
  filter["data"]["city"]["name"] = true;
  filter["data"]["iaqi"] = true;
  filter["data"]["time"]["iso"] = true;
  filter["data"]["forecast"]["daily"]["pm25"] = true;

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (e) {
    err = String("WAQI JSON ") + e.c_str();
    return false;
  }
  if (strcmp(doc["status"] | "", "ok") != 0) {
    err = "WAQI rejected request (token?)";
    return false;
  }

  JsonObject data = doc["data"];
  JsonVariant aqiV = data["aqi"];
  int aqi = -1;
  if (aqiV.is<int>()) {
    aqi = aqiV.as<int>();
  } else if (aqiV.is<const char*>()) {
    const char* str = aqiV.as<const char*>();
    if (str && isdigit((unsigned char)str[0])) aqi = atoi(str);
  }
  if (aqi < 0) {
    err = "station has no current reading";
    return false;
  }

  d = AqiData();
  d.valid = true;
  d.aqi = aqi;
  d.source = DataSource::WAQI;
  d.fetched = time(nullptr);
  strlcpy(d.dominant, data["dominentpol"] | "", sizeof(d.dominant));
  strlcpy(d.station, data["city"]["name"] | "Unknown station", sizeof(d.station));

  JsonObject iaqi = data["iaqi"];
  for (int i = 0; i < POLL_COUNT; i++) {
    JsonVariant v = iaqi[pollutant_key(i)]["v"];
    if (v.is<float>()) d.sub[i] = (int16_t)lroundf(v.as<float>());
  }
  if (iaqi["t"]["v"].is<float>()) d.tempC = iaqi["t"]["v"].as<float>();
  if (iaqi["h"]["v"].is<float>()) d.humidity = iaqi["h"]["v"].as<float>();

  d.observed = parseIso(data["time"]["iso"] | "");

  // Tomorrow's PM2.5 peak from WAQI's daily forecast (AQI scale)
  if (timeValid()) {
    time_t tm1 = time(nullptr) + 86400;
    struct tm lt;
    localtime_r(&tm1, &lt);
    char day[12];
    strftime(day, sizeof(day), "%Y-%m-%d", &lt);
    for (JsonObject f : data["forecast"]["daily"]["pm25"].as<JsonArray>()) {
      if (strcmp(f["day"] | "", day) == 0 && f["max"].is<int>()) {
        d.tomorrowPeak = f["max"].as<int>();
        break;
      }
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Open-Meteo (CAMS model): fallback headline + 24 h trend + tomorrow peak
// ---------------------------------------------------------------------------
static bool fetchOpenMeteo(const Settings& s, AqiData& d, String& err) {
  char url[420];
  snprintf(url, sizeof(url),
           "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f"
           "&current=us_aqi,us_aqi_pm2_5,us_aqi_pm10,us_aqi_ozone,us_aqi_nitrogen_dioxide,"
           "us_aqi_sulphur_dioxide,us_aqi_carbon_monoxide"
           "&hourly=us_aqi&past_days=1&forecast_days=2&timezone=auto",
           s.lat, s.lon);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(15000);
  if (!http.begin(client, url)) {
    err = "Open-Meteo connect failed";
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    err = "Open-Meteo HTTP " + String(code);
    http.end();
    return false;
  }

  JsonDocument filter;
  filter["utc_offset_seconds"] = true;
  filter["current"] = true;
  filter["hourly"]["time"] = true;
  filter["hourly"]["us_aqi"] = true;

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (e) {
    err = String("Open-Meteo JSON ") + e.c_str();
    return false;
  }

  long utcOff = doc["utc_offset_seconds"] | 0L;
  JsonObject cur = doc["current"];
  const char* curTime = cur["time"] | "";
  if (!curTime[0]) {
    err = "Open-Meteo: no current data";
    return false;
  }

  d = AqiData();
  d.source = DataSource::OpenMeteo;
  d.fetched = time(nullptr);
  d.observed = parseIso(curTime, utcOff);
  strlcpy(d.station, "Open-Meteo (CAMS model)", sizeof(d.station));

  static const char* kOmKeys[POLL_COUNT] = {"us_aqi_pm2_5", "us_aqi_pm10", "us_aqi_ozone",
                                            "us_aqi_nitrogen_dioxide", "us_aqi_sulphur_dioxide",
                                            "us_aqi_carbon_monoxide"};
  int best = -1;
  for (int i = 0; i < POLL_COUNT; i++) {
    JsonVariant v = cur[kOmKeys[i]];
    if (v.is<float>()) {
      d.sub[i] = (int16_t)lroundf(v.as<float>());
      if (best < 0 || d.sub[i] > d.sub[best]) best = i;
    }
  }
  if (best >= 0) strlcpy(d.dominant, pollutant_key(best), sizeof(d.dominant));

  // Hourly series: find the current hour, take 12 h either side
  JsonArray times = doc["hourly"]["time"];
  JsonArray vals = doc["hourly"]["us_aqi"];
  int n = (int)times.size();
  int idx = -1;
  for (int i = 0; i < n; i++) {
    const char* t = times[i] | "";
    if (strncmp(t, curTime, 13) == 0) {
      idx = i;
      break;
    }
  }

  if (cur["us_aqi"].is<float>()) {
    d.aqi = (int)lroundf(cur["us_aqi"].as<float>());
  } else if (idx >= 0 && vals[idx].is<float>()) {
    d.aqi = (int)lroundf(vals[idx].as<float>());
  }
  d.valid = d.aqi >= 0;

  if (idx >= 0) {
    int have = 0;
    for (int i = 0; i < TREND_POINTS; i++) {
      int j = idx - TREND_NOW_IDX + i;
      d.trend[i] = -1;
      if (j >= 0 && j < n && vals[j].is<float>()) {
        d.trend[i] = (int16_t)lroundf(vals[j].as<float>());
        have++;
      }
    }
    d.trendValid = have >= 4;

    // Tomorrow = every entry whose date is after today's (ISO strings sort correctly)
    char today[11];
    strlcpy(today, curTime, sizeof(today));
    for (int i = 0; i < n; i++) {
      const char* t = times[i] | "";
      if (strncmp(t, today, 10) > 0 && vals[i].is<float>()) {
        int v = (int)lroundf(vals[i].as<float>());
        if (v > d.tomorrowPeak) d.tomorrowPeak = v;
      }
    }
  }

  if (!d.valid) {
    err = "Open-Meteo: AQI missing";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// MQTT + Home Assistant discovery
// ---------------------------------------------------------------------------
static void mqttPublishDiscovery() {
  struct S {
    const char* key;
    const char* name;
    const char* devClass;
    bool measurement;
    const char* icon;
  };
  static const S sensors[] = {
      {"aqi", "AQI", "aqi", true, nullptr},
      {"pm25", "PM2.5 AQI", nullptr, true, "mdi:blur"},
      {"pm10", "PM10 AQI", nullptr, true, "mdi:blur-linear"},
      {"o3", "Ozone AQI", nullptr, true, "mdi:weather-sunny-alert"},
      {"no2", "NO2 AQI", nullptr, true, "mdi:molecule"},
      {"tomorrow_peak", "Tomorrow peak AQI", nullptr, true, "mdi:calendar-arrow-right"},
      {"dominant", "Dominant pollutant", nullptr, false, "mdi:alert-circle-outline"},
      {"station", "Station", nullptr, false, "mdi:map-marker"},
      {"source", "Data source", nullptr, false, "mdi:database"},
  };

  for (const S& s : sensors) {
    JsonDocument j;
    j["name"] = s.name;
    j["uniq_id"] = String(s_devId) + "_" + s.key;
    j["stat_t"] = s_stateTopic;
    j["val_tpl"] = String("{{ value_json.") + s.key + " if value_json." + s.key +
                   " is defined else none }}";
    j["avty_t"] = s_availTopic;
    if (s.devClass) j["dev_cla"] = s.devClass;
    if (s.measurement) j["stat_cla"] = "measurement";
    if (s.icon) j["ic"] = s.icon;
    JsonObject dev = j["dev"].to<JsonObject>();
    dev["ids"].add(s_devId);
    dev["name"] = "AQI Monitor";
    dev["mf"] = "DIY";
    dev["mdl"] = "Waveshare ESP32-S3-Touch-LCD-1.28";
    dev["sw"] = FW_VERSION;

    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/%s/config", s_devId, s.key);
    char buf[640];
    size_t len = serializeJson(j, buf, sizeof(buf));
    s_mqtt.publish(topic, (const uint8_t*)buf, len, true);
  }
  s_discoverySent = true;
}

static void mqttPublishState() {
  if (!s_mqtt.connected() || !s_st.data.valid) return;
  const AqiData& d = s_st.data;
  JsonDocument j;
  j["aqi"] = d.aqi;
  for (int i = 0; i < POLL_COUNT; i++)
    if (d.sub[i] >= 0) j[pollutant_key(i)] = d.sub[i];
  if (d.tomorrowPeak >= 0) j["tomorrow_peak"] = d.tomorrowPeak;
  j["dominant"] = dominant_label(d.dominant);
  j["station"] = d.station;
  j["source"] = d.source == DataSource::WAQI ? "waqi" : "open-meteo";
  char buf[512];
  size_t len = serializeJson(j, buf, sizeof(buf));
  s_mqtt.publish(s_stateTopic, (const uint8_t*)buf, len, true);
}

static void mqttLoop() {
  Settings s = settings_get();
  if (!s.mqttHost[0]) {
    if (s_mqtt.connected()) s_mqtt.disconnect();
    return;
  }
  if (s_mqtt.connected()) {
    s_mqtt.loop();
    return;
  }
  if (millis() - s_mqttLastTry < 15000 && s_mqttLastTry != 0) return;
  s_mqttLastTry = millis();

  s_mqtt.setServer(s.mqttHost, s.mqttPort);
  s_mqtt.setBufferSize(1024);
  bool ok = s.mqttUser[0]
                ? s_mqtt.connect(s_devId, s.mqttUser, s.mqttPass, s_availTopic, 0, true, "offline")
                : s_mqtt.connect(s_devId, nullptr, nullptr, s_availTopic, 0, true, "offline");
  if (ok) {
    Serial.println("[mqtt] connected");
    s_mqtt.publish(s_availTopic, "online", true);
    mqttPublishDiscovery();
    mqttPublishState();
  } else {
    Serial.printf("[mqtt] connect failed, rc=%d\n", s_mqtt.state());
  }
}

// ---------------------------------------------------------------------------
// Fetch cycle
// ---------------------------------------------------------------------------
static bool doFetch() {
  Settings s = settings_get();
  s_st.fetching = true;
  publish();

  AqiData w, o;
  String errW, errO;
  bool okW = fetchWaqi(s, w, errW);
  bool okO = fetchOpenMeteo(s, o, errO);

  s_st.fetching = false;
  if (okW) {
    if (okO) {
      w.trendValid = o.trendValid;
      memcpy(w.trend, o.trend, sizeof(w.trend));
      if (w.tomorrowPeak < 0) w.tomorrowPeak = o.tomorrowPeak;
    }
    s_st.data = w;
    s_st.lastError[0] = 0;
  } else if (okO) {
    s_st.data = o;
    if (s.waqiToken[0]) setError("WAQI: %s (showing model)", errW.c_str());
    else s_st.lastError[0] = 0;
  } else {
    setError("WAQI: %s / OM: %s", errW.c_str(), errO.c_str());
    publish();
    return false;
  }

  Serial.printf("[net] AQI %d from %s (%s)\n", s_st.data.aqi,
                s_st.data.source == DataSource::WAQI ? "WAQI" : "Open-Meteo", s_st.data.station);
  publish();
  mqttPublishState();
  return true;
}

// ---------------------------------------------------------------------------
// WiFiManager setup page
// ---------------------------------------------------------------------------
static const char kStationFinder[] = R"HTML(
<div style='margin:10px 0;padding:8px;border:1px solid #555;border-radius:6px'>
<b>Find a WAQI station</b><br><small>Works once the device is on your WiFi (open http://aqi-monitor.local). Enter your token above first.</small><br>
<input id='q' placeholder='e.g. lahore' style='width:60%'>
<button type='button' style='width:35%' onclick='fs()'>Search</button>
<div id='r' style='font-size:13px;margin-top:6px'></div></div>
<script>
function fs(){var t=document.getElementById('waqi_token').value.trim();var q=document.getElementById('q').value.trim();var r=document.getElementById('r');
if(!t){r.innerHTML='Enter your WAQI token first.';return;}r.innerHTML='Searching...';
fetch('https://api.waqi.info/search/?keyword='+encodeURIComponent(q)+'&token='+encodeURIComponent(t)).then(function(x){return x.json();}).then(function(j){
if(j.status!='ok'){r.innerHTML='Error: '+j.data;return;}r.innerHTML='Tap a station to select it:';
j.data.slice(0,15).forEach(function(s){var a=document.createElement('a');a.href='#';a.textContent='AQI '+s.aqi+'  -  '+s.station.name;a.style.display='block';a.style.margin='4px 0';
a.onclick=function(){document.getElementById('station').value='@'+s.uid;return false;};r.appendChild(a);});
}).catch(function(e){r.innerHTML='Search failed ('+e+'). Use the map at aqicn.org instead.';});}
</script>)HTML";

static void trimInto(char* dst, size_t len, const char* src) {
  while (*src == ' ' || *src == '\t') src++;
  strlcpy(dst, src, len);
  size_t n = strlen(dst);
  while (n && (dst[n - 1] == ' ' || dst[n - 1] == '\t')) dst[--n] = 0;
}

static void onSaveParams() {
  Settings s = settings_get();
  trimInto(s.waqiToken, sizeof(s.waqiToken), p_token->getValue());
  trimInto(s.station, sizeof(s.station), p_station->getValue());
  float lat = atof(p_lat->getValue()), lon = atof(p_lon->getValue());
  if (lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180 && (lat != 0 || lon != 0)) {
    s.lat = lat;
    s.lon = lon;
  }
  s.refreshMin = constrain(atoi(p_refresh->getValue()), 5, 120);
  trimInto(s.tz, sizeof(s.tz), p_tz->getValue());
  if (!s.tz[0]) strlcpy(s.tz, "PKT-5", sizeof(s.tz));
  s.nightStart = constrain(atoi(p_nstart->getValue()), -1, 23);
  s.nightEnd = constrain(atoi(p_nend->getValue()), 0, 23);
  s.nightBrightness = constrain(atoi(p_nbri->getValue()), 0, 255);
  s.screenTimeoutMin = constrain(atoi(p_sto->getValue()), 0, 1440);
  trimInto(s.mqttHost, sizeof(s.mqttHost), p_mhost->getValue());
  int port = atoi(p_mport->getValue());
  s.mqttPort = (port > 0 && port < 65536) ? port : 1883;
  trimInto(s.mqttUser, sizeof(s.mqttUser), p_muser->getValue());
  trimInto(s.mqttPass, sizeof(s.mqttPass), p_mpass->getValue());
  settings_set(s, true);

  configTzTime(s.tz, "pool.ntp.org", "time.google.com");
  if (s_mqtt.connected()) s_mqtt.disconnect();
  s_mqttLastTry = 0;
  s_discoverySent = false;
  Serial.println("[net] settings saved");
  xTaskNotifyGive(s_task);  // fetch again with the new settings
}

static void buildPortal() {
  Settings s = settings_get();
  char buf[24];

  p_token = new WiFiManagerParameter("waqi_token", "WAQI token (free: aqicn.org/data-platform/token)",
                                     s.waqiToken, 63);
  p_station = new WiFiManagerParameter("station", "WAQI station (@uid, blank = nearest to location)",
                                       s.station, 23);
  snprintf(buf, sizeof(buf), "%.5f", s.lat);
  p_lat = new WiFiManagerParameter("lat", "Latitude", buf, 12);
  snprintf(buf, sizeof(buf), "%.5f", s.lon);
  p_lon = new WiFiManagerParameter("lon", "Longitude", buf, 12);
  snprintf(buf, sizeof(buf), "%u", s.refreshMin);
  p_refresh = new WiFiManagerParameter("refresh", "Refresh interval, minutes (5-120)", buf, 4);
  p_tz = new WiFiManagerParameter("tz", "POSIX timezone (Pakistan = PKT-5)", s.tz, 39);
  snprintf(buf, sizeof(buf), "%d", s.nightStart);
  p_nstart = new WiFiManagerParameter("nstart", "Night dim from hour (0-23, -1 = off)", buf, 3);
  snprintf(buf, sizeof(buf), "%d", s.nightEnd);
  p_nend = new WiFiManagerParameter("nend", "Night dim until hour (0-23)", buf, 3);
  snprintf(buf, sizeof(buf), "%u", s.nightBrightness);
  p_nbri = new WiFiManagerParameter("nbri", "Night brightness (0-255)", buf, 4);
  snprintf(buf, sizeof(buf), "%u", s.screenTimeoutMin);
  p_sto = new WiFiManagerParameter("sto", "Screen off after N idle minutes (0 = never)", buf, 5);
  p_mhost = new WiFiManagerParameter("mhost", "MQTT host (blank = disabled)", s.mqttHost, 63);
  snprintf(buf, sizeof(buf), "%u", s.mqttPort);
  p_mport = new WiFiManagerParameter("mport", "MQTT port", buf, 6);
  p_muser = new WiFiManagerParameter("muser", "MQTT user", s.mqttUser, 31);
  p_mpass = new WiFiManagerParameter("mpass", "MQTT password", s.mqttPass, 63, "type='password'");

  static WiFiManagerParameter hData("<h3>Air quality data</h3>");
  static WiFiManagerParameter finder(kStationFinder);
  static WiFiManagerParameter hDisp("<h3>Display</h3>");
  static WiFiManagerParameter hMqtt("<h3>Home Assistant (MQTT, optional)</h3>");

  wm.addParameter(&hData);
  wm.addParameter(p_token);
  wm.addParameter(&finder);
  wm.addParameter(p_station);
  wm.addParameter(p_lat);
  wm.addParameter(p_lon);
  wm.addParameter(p_refresh);
  wm.addParameter(&hDisp);
  wm.addParameter(p_tz);
  wm.addParameter(p_nstart);
  wm.addParameter(p_nend);
  wm.addParameter(p_nbri);
  wm.addParameter(p_sto);
  wm.addParameter(&hMqtt);
  wm.addParameter(p_mhost);
  wm.addParameter(p_mport);
  wm.addParameter(p_muser);
  wm.addParameter(p_mpass);

  std::vector<const char*> menu = {"wifi", "param", "info", "sep", "restart"};
  wm.setMenu(menu);
  wm.setTitle("AQI Monitor");
  wm.setClass("invert");
  wm.setParamsPage(true);
  wm.setSaveParamsCallback(onSaveParams);
  wm.setHostname(DEVICE_HOSTNAME);
  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(20);
  // With saved credentials, close the portal after 3 min and keep retrying the router.
  if (wm.getWiFiIsSaved()) wm.setConfigPortalTimeout(180);
}

static void startServices() {
  ArduinoOTA.setHostname(DEVICE_HOSTNAME);
  ArduinoOTA.onStart([]() {
    s_st.otaProgress = 0;
    publish();
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    int pct = total ? (int)(done * 100ULL / total) : 0;
    if (pct >= s_st.otaProgress + 2) {
      s_st.otaProgress = pct;
      publish();
    }
  });
  ArduinoOTA.onEnd([]() {
    s_st.otaProgress = 100;
    publish();
  });
  ArduinoOTA.onError([](ota_error_t e) {
    s_st.otaProgress = -1;
    setError("OTA failed (%d)", (int)e);
    publish();
  });
  ArduinoOTA.begin();  // also starts mDNS as aqi-monitor.local
  MDNS.addService("http", "tcp", 80);

  wm.startWebPortal();  // settings page at http://aqi-monitor.local/
  Serial.printf("[net] online: http://%s/  (http://%s.local/)\n", WiFi.localIP().toString().c_str(),
                DEVICE_HOSTNAME);
}

// ---------------------------------------------------------------------------
// Task
// ---------------------------------------------------------------------------
static void netTask(void*) {
  Settings s = settings_get();
  configTzTime(s.tz, "pool.ntp.org", "time.google.com");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  // Full TX power makes the soft-AP invisible or flaky on some ESP32-S3 boards
  // with compact antennas / weak USB supply. 8.5 dBm is plenty indoors.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

#ifdef WIFI_SSID
  // Optional: credentials baked in at build time (see platformio.ini).
  // Used only when nothing is saved yet; WiFi.begin() stores them for next boot.
  if (!wm.getWiFiIsSaved()) {
    Serial.printf("[net] using build-time WiFi '%s'\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) vTaskDelay(pdMS_TO_TICKS(250));
  }
#endif

  uint32_t mac = (uint32_t)(ESP.getEfuseMac() >> 24) & 0xFFFFFF;
  snprintf(s_devId, sizeof(s_devId), "aqimon_%06lx", (unsigned long)mac);
  snprintf(s_stateTopic, sizeof(s_stateTopic), "aqimon/%s/state", s_devId);
  snprintf(s_availTopic, sizeof(s_availTopic), "aqimon/%s/status", s_devId);
  snprintf(s_st.apName, sizeof(s_st.apName), "AQI-Setup-%04lX", (unsigned long)(mac & 0xFFFF));

  buildPortal();

  s_st.phase = NetPhase::Connecting;
  publish();
  wm.autoConnect(s_st.apName);  // non-blocking: opens the setup AP if it can't connect
  WiFi.setTxPower(WIFI_POWER_8_5dBm);  // re-apply: WiFiManager switches mode when starting the AP
  if (wm.getConfigPortalActive())
    Serial.printf("[net] setup AP '%s' started at %s\n", s_st.apName, WiFi.softAPIP().toString().c_str());

  bool servicesStarted = false;
  bool everOnline = false;
  uint32_t nextFetch = 0;
  uint32_t backoff = 30000;
  uint32_t lastReconnect = millis();

  for (;;) {
    wm.process();

    if (s_resetWifi) {
      wm.resetSettings();
      delay(300);
      ESP.restart();
    }

    bool online = WiFi.status() == WL_CONNECTED;
    bool portal = wm.getConfigPortalActive();

    if (online && !portal && !servicesStarted) {
      startServices();
      servicesStarted = true;
    }

    NetPhase ph = online ? NetPhase::Online
                         : portal ? NetPhase::SetupPortal
                                  : (everOnline ? NetPhase::Offline : NetPhase::Connecting);
    if (online) everOnline = true;
    if (ph != s_st.phase) {
      s_st.phase = ph;
      if (online) strlcpy(s_st.ip, WiFi.localIP().toString().c_str(), sizeof(s_st.ip));
      publish();
    }

    if (online) {
      ArduinoOTA.handle();
      mqttLoop();
    } else if (!portal && wm.getWiFiIsSaved() && millis() - lastReconnect > 30000) {
      lastReconnect = millis();
      WiFi.begin();  // retry saved credentials (e.g. router was down at boot)
    }

    bool forced = ulTaskNotifyTake(pdTRUE, 0) > 0;
    if (online && s_st.otaProgress < 0 && (forced || (int32_t)(millis() - nextFetch) >= 0)) {
      if (doFetch()) {
        nextFetch = millis() + settings_get().refreshMin * 60000UL;
        backoff = 30000;
      } else {
        nextFetch = millis() + backoff;
        uint32_t cap = settings_get().refreshMin * 60000UL;
        backoff = (backoff * 2 < cap) ? backoff * 2 : cap;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void net_start() {
  s_q = xQueueCreate(1, sizeof(AppState));
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, &s_task, 0);
}

bool net_poll_state(AppState& out) { return s_q && xQueueReceive(s_q, &out, 0) == pdTRUE; }

void net_request_refresh() {
  if (s_task) xTaskNotifyGive(s_task);
}

void net_request_wifi_reset() { s_resetWifi = true; }
