// AQI Monitor - Waveshare ESP32-S3-Touch-LCD-1.28
//
// Core 1 (Arduino loop): LVGL rendering, touch, IMU, backlight.
// Core 0 (net task):     WiFi, HTTPS fetches, OTA, MQTT.  See net.cpp.

#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <math.h>
#include <time.h>

#include "aqi_scale.h"
#include "imu_qmi8658.h"
#include "lgfx_board.h"
#include "net.h"
#include "pins.h"
#include "settings.h"
#include "touch_cst816.h"
#include "ui.h"
#include "version.h"

static LGFX lcd;
static lv_display_t* disp;

static constexpr uint32_t BUF_LINES = 40;
static constexpr size_t BUF_BYTES = LCD_W * BUF_LINES * sizeof(uint16_t);

static uint32_t lastActivity = 0;
static bool screenOn = true;
static bool swallowTouch = false;
static int currentBl = -1;
static int lastBand = -1;
static bool imuOk = false;

// ---------------------------------------------------------------------------
// LVGL glue
// ---------------------------------------------------------------------------
static uint32_t tick_cb() { return millis(); }

static void flush_cb(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
  const uint32_t w = lv_area_get_width(area);
  const uint32_t h = lv_area_get_height(area);
  lcd.startWrite();
  lcd.setAddrWindow(area->x1, area->y1, w, h);
  lcd.writePixels((lgfx::rgb565_t*)px, w * h);
  lcd.endWrite();
  lv_display_flush_ready(d);
}

static void wake_screen() {
  lastActivity = millis();
  if (!screenOn) {
    screenOn = true;
    currentBl = -1;  // force the backlight manager to re-apply
  }
}

static void touch_cb(lv_indev_t*, lv_indev_data_t* data) {
  uint16_t x, y;
  if (touch_read(x, y)) {
    lastActivity = millis();
    if (!screenOn) {
      // The first touch only wakes the screen; don't let it press anything.
      wake_screen();
      swallowTouch = true;
    }
    if (swallowTouch) {
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    }
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    swallowTouch = false;
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// ---------------------------------------------------------------------------
// Backlight: user brightness, night dimming, idle timeout
// ---------------------------------------------------------------------------
static bool is_night(const Settings& s) {
  if (s.nightStart < 0) return false;
  struct tm lt;
  if (!getLocalTime(&lt, 0)) return false;
  int h = lt.tm_hour;
  if (s.nightStart == s.nightEnd) return false;
  if (s.nightStart < s.nightEnd) return h >= s.nightStart && h < s.nightEnd;
  return h >= s.nightStart || h < s.nightEnd;  // window crosses midnight
}

static void manage_backlight() {
  Settings s = settings_get();
  if (screenOn && s.screenTimeoutMin > 0 && millis() - lastActivity > s.screenTimeoutMin * 60000UL) {
    screenOn = false;
  }
  int target = 0;
  if (screenOn) target = is_night(s) ? s.nightBrightness : s.brightness;
  if (target != currentBl) {
    lcd.setBrightness(target);
    currentBl = target;
  }
}

// ---------------------------------------------------------------------------
// IMU: wake the screen when the device is picked up or bumped
// ---------------------------------------------------------------------------
static void poll_imu() {
  static float bx, by, bz;
  static bool init = false;
  float ax, ay, az;
  if (!imu_read_accel(ax, ay, az)) return;
  if (!init) {
    bx = ax; by = ay; bz = az;
    init = true;
    return;
  }
  float delta = fabsf(ax - bx) + fabsf(ay - by) + fabsf(az - bz);
  bx += (ax - bx) * 0.2f;
  by += (ay - by) * 0.2f;
  bz += (az - bz) * 0.2f;
  if (delta > 0.25f) wake_screen();
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.printf("\nAQI Monitor %s\n", FW_VERSION);

  settings_begin();

  lcd.init();
  lcd.setRotation(0);
  lcd.setBrightness(0);
  lcd.fillScreen(TFT_BLACK);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  bool touchOk = touch_begin();
  imuOk = imu_begin();
  Serial.printf("touch: %s, imu: %s, psram: %u bytes\n", touchOk ? "ok" : "MISSING",
                imuOk ? "ok" : "missing", (unsigned)ESP.getPsramSize());

  lv_init();
  lv_tick_set_cb(tick_cb);

  disp = lv_display_create(LCD_W, LCD_H);
  void* buf1 = heap_caps_malloc(BUF_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void* buf2 = heap_caps_malloc(BUF_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_buffers(disp, buf1, buf2, BUF_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_cb);

  ui_init();
  lv_timer_handler();  // draw the first frame before the backlight comes on

  lastActivity = millis();
  manage_backlight();

  net_start();
}

void loop() {
  lv_timer_handler();

  AppState st;
  if (net_poll_state(st)) {
    ui_apply_state(st);
    if (st.data.valid) {
      int band = aqi_band_index(st.data.aqi);
      if (lastBand >= 0 && band != lastBand) wake_screen();  // air quality category changed
      lastBand = band;
    }
    if (st.otaProgress >= 0) wake_screen();
  }

  static uint32_t lastImu = 0, lastBl = 0;
  uint32_t now = millis();
  if (imuOk && now - lastImu >= 100) {
    lastImu = now;
    poll_imu();
  }
  if (now - lastBl >= 250) {
    lastBl = now;
    manage_backlight();
  }

  static uint32_t lastTemp = 0;
  if (now - lastTemp >= 60000) {
    lastTemp = now;
    Serial.printf("[sys] chip temp %.1f C, free heap %u\n", temperatureRead(), (unsigned)ESP.getFreeHeap());
  }

  // Sleep longer when the screen is off: fewer wake-ups, less heat.
  delay(screenOn ? 10 : 40);
}
