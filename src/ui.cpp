// LVGL 9 UI for the 240x240 round display.
// Five swipeable tiles: Home gauge, Pollutant ring, 24 h trend, Health advice, Settings.
// Layout keeps content inside the visible circle (radius 120 around 120,120).

#include "ui.h"

#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include <time.h>

#include "aqi_scale.h"
#include "net.h"
#include "settings.h"
#include "version.h"

enum { TILE_HOME = 0, TILE_POLL, TILE_TREND, TILE_HEALTH, TILE_SETTINGS, TILE_COUNT };

static const lv_color_t C_BG = lv_color_hex(0x05080C);
static const lv_color_t C_TRACK = lv_color_hex(0x1C232B);
static const lv_color_t C_TEXT = lv_color_hex(0xF2F5F8);
static const lv_color_t C_MUTED = lv_color_hex(0x8A96A3);

static lv_obj_t* tv;
static lv_obj_t* tiles[TILE_COUNT];
static lv_obj_t* dots[TILE_COUNT];

// Home
static lv_obj_t *h_arc, *h_val, *h_cat, *h_dom, *h_env, *h_stn, *h_fresh, *h_dot;
static int32_t h_shown = 0;

// Pollutants
static lv_obj_t* p_arcs[POLL_COUNT];
static lv_obj_t* p_lbls[POLL_COUNT];
static lv_obj_t *p_name, *p_val, *p_band, *p_unit;
static int p_sel = POL_PM25;
static uint32_t p_userAt = 0;

// Trend
static lv_obj_t *t_chart, *t_none, *t_chip, *t_chipLbl, *t_nowLine;
static lv_chart_series_t* t_ser;

// Health
static lv_obj_t *hl_icon, *hl_title, *hl_text;

// Settings
static lv_obj_t *s_slider, *s_ip, *s_err, *s_resetBtn, *s_resetLbl;
static uint32_t s_resetArmedAt = 0;

// Overlay
static lv_obj_t *ov, *ov_lbl;
static bool ov_dismissed = false;
static NetPhase ov_lastPhase = NetPhase::Booting;

static AppState st;
static bool haveState = false;
static time_t lastDataStamp = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static lv_obj_t* mk_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, int y,
                          const char* text = "") {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(l, text);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
  return l;
}

static lv_obj_t* mk_arc(lv_obj_t* parent, int size, int width) {
  lv_obj_t* a = lv_arc_create(parent);
  lv_obj_set_size(a, size, size);
  lv_obj_center(a);
  lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, C_TRACK, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
  lv_obj_set_style_pad_all(a, 0, 0);
  return a;
}

static lv_color_t band_color(int aqi) { return lv_color_hex(aqi_band(aqi).color); }

static void set_screen_tint(int aqi) {
  lv_obj_t* scr = lv_screen_active();
  lv_color_t top = aqi >= 0 ? lv_color_mix(band_color(aqi), C_BG, 60) : C_BG;
  lv_obj_set_style_bg_color(scr, top, 0);
  lv_obj_set_style_bg_grad_color(scr, C_BG, 0);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_main_stop(scr, 0, 0);
  lv_obj_set_style_bg_grad_stop(scr, 160, 0);
}

static bool on_tile(int idx) { return lv_tileview_get_tile_active(tv) == tiles[idx]; }

// ---------------------------------------------------------------------------
// Home tile
// ---------------------------------------------------------------------------
static void home_anim_cb(void*, int32_t v) {
  lv_arc_set_value(h_arc, v > 500 ? 500 : v);
  lv_label_set_text_fmt(h_val, "%d", (int)v);
  lv_obj_set_style_arc_color(h_arc, band_color(v), LV_PART_INDICATOR);
}

static void home_refresh_long_press(lv_event_t*) { net_request_refresh(); }

static void build_home(lv_obj_t* t) {
  h_arc = mk_arc(t, 226, 14);
  lv_arc_set_rotation(h_arc, 135);
  lv_arc_set_bg_angles(h_arc, 0, 270);
  lv_arc_set_range(h_arc, 0, 500);
  lv_arc_set_value(h_arc, 0);

  h_env = mk_label(t, &lv_font_montserrat_12, C_MUTED, -66);
  h_dom = mk_label(t, &lv_font_montserrat_14, C_MUTED, -42, "US AQI");
  h_val = mk_label(t, &lv_font_montserrat_48, C_TEXT, -6, "--");
  h_cat = mk_label(t, &lv_font_montserrat_16, C_TEXT, 32, "Connecting...");

  h_stn = mk_label(t, &lv_font_montserrat_12, C_MUTED, 62);
  lv_obj_set_width(h_stn, 128);
  lv_label_set_long_mode(h_stn, LV_LABEL_LONG_DOT);

  h_fresh = mk_label(t, &lv_font_montserrat_12, C_MUTED, 80);
  h_dot = lv_obj_create(t);
  lv_obj_set_size(h_dot, 7, 7);
  lv_obj_set_style_radius(h_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(h_dot, 0, 0);
  lv_obj_set_style_bg_color(h_dot, C_MUTED, 0);
  lv_obj_remove_flag(h_dot, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_add_event_cb(t, home_refresh_long_press, LV_EVENT_LONG_PRESSED, nullptr);
}

static void home_update_fresh() {
  if (!haveState || !st.data.valid) {
    lv_label_set_text(h_fresh, st.fetching ? "Fetching..." : "No data yet");
    lv_obj_set_style_bg_color(h_dot, C_MUTED, 0);
  } else {
    const AqiData& d = st.data;
    char hhmm[8] = "--:--";
    if (d.observed > 0) {
      struct tm lt;
      localtime_r(&d.observed, &lt);
      strftime(hhmm, sizeof(hhmm), "%H:%M", &lt);
    }
    time_t now = time(nullptr);
    long ageMin = (now > 1700000000 && d.observed > 0) ? (long)((now - d.observed) / 60) : -1;
    bool offline = st.phase != NetPhase::Online;
    const char* src = d.source == DataSource::WAQI ? "Station" : "Model";

    lv_color_t c = lv_color_hex(0x3DDC84);
    if (offline || ageMin > 240) c = lv_color_hex(0xF2384A);
    else if (ageMin < 0) c = C_MUTED;
    else if (ageMin > 120) c = lv_color_hex(0xF7D038);
    lv_obj_set_style_bg_color(h_dot, c, 0);

    if (st.fetching) lv_label_set_text(h_fresh, "Updating...");
    else lv_label_set_text_fmt(h_fresh, "%s %s%s", src, hhmm, offline ? " | offline" : "");
  }
  lv_obj_update_layout(h_fresh);
  lv_obj_align_to(h_dot, h_fresh, LV_ALIGN_OUT_LEFT_MID, -5, 0);
}

static void home_update() {
  const AqiData& d = st.data;
  if (!d.valid) return;

  const AqiBand& b = aqi_band(d.aqi);
  lv_label_set_text(h_cat, b.shortName);
  lv_obj_set_style_text_color(h_cat, lv_color_hex(b.color), 0);

  const char* dom = dominant_label(d.dominant);
  if (dom[0]) lv_label_set_text_fmt(h_dom, "US AQI | %s", dom);
  else lv_label_set_text(h_dom, "US AQI");

  if (!isnan(d.tempC) && !isnan(d.humidity))
    lv_label_set_text_fmt(h_env, "%dC   %d%% RH", (int)lroundf(d.tempC), (int)lroundf(d.humidity));
  else if (!isnan(d.tempC))
    lv_label_set_text_fmt(h_env, "%dC", (int)lroundf(d.tempC));
  else
    lv_label_set_text(h_env, "");

  lv_label_set_text(h_stn, d.station);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, h_arc);
  lv_anim_set_exec_cb(&a, home_anim_cb);
  lv_anim_set_values(&a, h_shown, d.aqi);
  lv_anim_set_duration(&a, 900);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
  h_shown = d.aqi;

  set_screen_tint(d.aqi);
}

// ---------------------------------------------------------------------------
// Pollutant ring tile
// ---------------------------------------------------------------------------
static void poll_select(int i) {
  p_sel = i;
  const AqiData& d = st.data;
  for (int k = 0; k < POLL_COUNT; k++) {
    int w = (k == i) ? 14 : 8;
    lv_obj_set_style_arc_width(p_arcs[k], w, LV_PART_MAIN);
    lv_obj_set_style_arc_width(p_arcs[k], w, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(p_lbls[k], k == i ? C_TEXT : C_MUTED, 0);
  }
  lv_label_set_text(p_name, pollutant_label(i));
  int v = d.sub[i];
  if (v >= 0) {
    lv_label_set_text_fmt(p_val, "%d", v);
    lv_label_set_text(p_band, aqi_band(v).shortName);
    lv_obj_set_style_text_color(p_band, band_color(v), 0);
    lv_obj_set_style_text_color(p_val, C_TEXT, 0);
  } else {
    lv_label_set_text(p_val, "--");
    lv_label_set_text(p_band, "not reported");
    lv_obj_set_style_text_color(p_band, C_MUTED, 0);
    lv_obj_set_style_text_color(p_val, C_MUTED, 0);
  }
}

static void poll_click(lv_event_t*) {
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  float dx = p.x - 120.0f, dy = p.y - 120.0f;
  int next;
  if (dx * dx + dy * dy < 50.0f * 50.0f) {
    next = (p_sel + 1) % POLL_COUNT;  // tap the centre to step through
  } else {
    float ang = atan2f(dy, dx) * 180.0f / (float)M_PI;  // -180..180, 0 = 3 o'clock
    if (ang < 0) ang += 360.0f;
    next = ((int)fmodf(ang - 270.0f + 360.0f, 360.0f)) / 60;
  }
  p_userAt = millis();
  poll_select(next % POLL_COUNT);
}

static void build_poll(lv_obj_t* t) {
  for (int i = 0; i < POLL_COUNT; i++) {
    lv_obj_t* a = mk_arc(t, 222, 8);
    lv_arc_set_rotation(a, 270 + i * 60 + 4);
    lv_arc_set_bg_angles(a, 0, 52);
    lv_arc_set_range(a, 0, 300);
    lv_arc_set_value(a, 0);
    p_arcs[i] = a;

    float mid = (270 + i * 60 + 30) * (float)M_PI / 180.0f;
    p_lbls[i] = mk_label(t, &lv_font_montserrat_12, C_MUTED, 0, pollutant_label(i));
    lv_obj_align(p_lbls[i], LV_ALIGN_CENTER, (int)lroundf(84 * cosf(mid)), (int)lroundf(84 * sinf(mid)));
  }
  p_name = mk_label(t, &lv_font_montserrat_16, C_TEXT, -32, "PM2.5");
  p_val = mk_label(t, &lv_font_montserrat_28, C_TEXT, -4, "--");
  p_unit = mk_label(t, &lv_font_montserrat_12, C_MUTED, 20, "AQI sub-index");
  p_band = mk_label(t, &lv_font_montserrat_12, C_MUTED, 38, "");

  lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(t, poll_click, LV_EVENT_CLICKED, nullptr);
}

static void poll_update() {
  const AqiData& d = st.data;
  for (int i = 0; i < POLL_COUNT; i++) {
    int v = d.sub[i];
    lv_arc_set_value(p_arcs[i], v < 0 ? 0 : (v > 300 ? 300 : v));
    lv_obj_set_style_arc_color(p_arcs[i], v < 0 ? C_TRACK : band_color(v), LV_PART_INDICATOR);
  }
  poll_select(p_sel);
}

static void poll_cycle_timer(lv_timer_t*) {
  if (!haveState || !st.data.valid || !on_tile(TILE_POLL)) return;
  if (millis() - p_userAt < 10000) return;
  for (int step = 1; step <= POLL_COUNT; step++) {
    int i = (p_sel + step) % POLL_COUNT;
    if (st.data.sub[i] >= 0) {
      poll_select(i);
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// Trend tile
// ---------------------------------------------------------------------------
static const int CHART_W = 188, CHART_H = 92;

static void build_trend(lv_obj_t* t) {
  mk_label(t, &lv_font_montserrat_14, C_TEXT, -82, "AQI trend");
  mk_label(t, &lv_font_montserrat_12, C_MUTED, -64, "CAMS model, hourly");

  t_chart = lv_chart_create(t);
  lv_obj_set_size(t_chart, CHART_W, CHART_H);
  lv_obj_align(t_chart, LV_ALIGN_CENTER, 0, 2);
  lv_chart_set_type(t_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(t_chart, TREND_POINTS);
  lv_chart_set_div_line_count(t_chart, 3, 0);
  lv_obj_set_style_bg_opa(t_chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(t_chart, 0, 0);
  lv_obj_set_style_pad_all(t_chart, 0, 0);
  lv_obj_set_style_line_color(t_chart, C_TRACK, LV_PART_MAIN);
  lv_obj_set_style_line_width(t_chart, 3, LV_PART_ITEMS);
  lv_obj_set_style_size(t_chart, 0, 0, LV_PART_INDICATOR);
  lv_obj_remove_flag(t_chart, LV_OBJ_FLAG_CLICKABLE);
  t_ser = lv_chart_add_series(t_chart, lv_color_hex(0xF2F5F8), LV_CHART_AXIS_PRIMARY_Y);

  t_nowLine = lv_obj_create(t_chart);
  lv_obj_set_size(t_nowLine, 2, CHART_H);
  lv_obj_set_pos(t_nowLine, (TREND_NOW_IDX * (CHART_W - 1)) / (TREND_POINTS - 1) - 1, 0);
  lv_obj_set_style_bg_color(t_nowLine, lv_color_hex(0x4FC3F7), 0);
  lv_obj_set_style_bg_opa(t_nowLine, LV_OPA_70, 0);
  lv_obj_set_style_border_width(t_nowLine, 0, 0);
  lv_obj_set_style_radius(t_nowLine, 0, 0);
  lv_obj_remove_flag(t_nowLine, LV_OBJ_FLAG_CLICKABLE);

  t_none = mk_label(t, &lv_font_montserrat_12, C_MUTED, 2, "No trend data");

  lv_obj_t* l1 = mk_label(t, &lv_font_montserrat_12, C_MUTED, 0, "-12h");
  lv_obj_align(l1, LV_ALIGN_CENTER, -80, 58);
  lv_obj_t* l2 = mk_label(t, &lv_font_montserrat_12, lv_color_hex(0x4FC3F7), 0, "now");
  lv_obj_align(l2, LV_ALIGN_CENTER, 4, 58);
  lv_obj_t* l3 = mk_label(t, &lv_font_montserrat_12, C_MUTED, 0, "+12h");
  lv_obj_align(l3, LV_ALIGN_CENTER, 80, 58);

  t_chip = lv_obj_create(t);
  lv_obj_set_size(t_chip, 156, 24);
  lv_obj_align(t_chip, LV_ALIGN_CENTER, 0, 82);
  lv_obj_set_style_radius(t_chip, 12, 0);
  lv_obj_set_style_border_width(t_chip, 0, 0);
  lv_obj_set_style_pad_all(t_chip, 0, 0);
  lv_obj_remove_flag(t_chip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(t_chip, LV_OBJ_FLAG_SCROLLABLE);
  t_chipLbl = lv_label_create(t_chip);
  lv_obj_set_style_text_font(t_chipLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(t_chipLbl, lv_color_hex(0x05080C), 0);
  lv_obj_center(t_chipLbl);
  lv_obj_add_flag(t_chip, LV_OBJ_FLAG_HIDDEN);
}

static void trend_update() {
  const AqiData& d = st.data;
  if (d.trendValid) {
    int mx = 0;
    for (int i = 0; i < TREND_POINTS; i++) {
      if (d.trend[i] > mx) mx = d.trend[i];
      lv_chart_set_value_by_id(t_chart, t_ser, i, d.trend[i] >= 0 ? d.trend[i] : LV_CHART_POINT_NONE);
    }
    int top = ((mx + 20) / 50 + 1) * 50;
    if (top < 100) top = 100;
    lv_chart_set_range(t_chart, LV_CHART_AXIS_PRIMARY_Y, 0, top);
    lv_chart_set_series_color(t_chart, t_ser, band_color(d.trend[TREND_NOW_IDX] >= 0 ? d.trend[TREND_NOW_IDX] : d.aqi));
    lv_chart_refresh(t_chart);
    lv_obj_add_flag(t_none, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(t_chart, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(t_chart, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(t_none, LV_OBJ_FLAG_HIDDEN);
  }

  if (d.tomorrowPeak >= 0) {
    lv_obj_set_style_bg_color(t_chip, band_color(d.tomorrowPeak), 0);
    lv_label_set_text_fmt(t_chipLbl, "Tomorrow peak %d", d.tomorrowPeak);
    lv_obj_remove_flag(t_chip, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(t_chip, LV_OBJ_FLAG_HIDDEN);
  }
}

// ---------------------------------------------------------------------------
// Health tile
// ---------------------------------------------------------------------------
static void build_health(lv_obj_t* t) {
  hl_icon = mk_label(t, &lv_font_montserrat_28, C_MUTED, -66, LV_SYMBOL_EYE_OPEN);
  hl_title = mk_label(t, &lv_font_montserrat_16, C_TEXT, -34, "Health advice");
  lv_obj_set_width(hl_title, 190);
  lv_label_set_long_mode(hl_title, LV_LABEL_LONG_WRAP);
  hl_text = mk_label(t, &lv_font_montserrat_14, C_TEXT, 0, "Waiting for data");
  lv_obj_set_width(hl_text, 176);
  lv_label_set_long_mode(hl_text, LV_LABEL_LONG_WRAP);
  lv_obj_align(hl_text, LV_ALIGN_TOP_MID, 0, 104);
}

static void health_update() {
  const AqiBand& b = aqi_band(st.data.aqi);
  lv_label_set_text(hl_icon, b.icon);
  lv_obj_set_style_text_color(hl_icon, lv_color_hex(b.color), 0);
  lv_label_set_text(hl_title, b.longName);
  lv_obj_set_style_text_color(hl_title, lv_color_hex(b.color), 0);
  lv_label_set_text(hl_text, b.tip);
}

// ---------------------------------------------------------------------------
// Settings tile
// ---------------------------------------------------------------------------
static void slider_cb(lv_event_t* e) {
  Settings s = settings_get();
  s.brightness = (uint8_t)lv_slider_get_value(s_slider);
  bool persist = lv_event_get_code(e) == LV_EVENT_RELEASED;
  settings_set(s, persist);
}

static void refresh_cb(lv_event_t*) { net_request_refresh(); }

static void reset_cb(lv_event_t*) {
  if (s_resetArmedAt && millis() - s_resetArmedAt < 5000) {
    lv_label_set_text(s_resetLbl, "Resetting...");
    net_request_wifi_reset();
    return;
  }
  s_resetArmedAt = millis();
  lv_label_set_text(s_resetLbl, "Hold again to confirm");
}

static void build_settings(lv_obj_t* t) {
  mk_label(t, &lv_font_montserrat_16, C_TEXT, -84, LV_SYMBOL_SETTINGS "  Settings");

  mk_label(t, &lv_font_montserrat_12, C_MUTED, -60, LV_SYMBOL_IMAGE "  Brightness");
  s_slider = lv_slider_create(t);
  lv_obj_set_size(s_slider, 150, 10);
  lv_obj_align(s_slider, LV_ALIGN_CENTER, 0, -40);
  lv_slider_set_range(s_slider, 10, 255);
  lv_slider_set_value(s_slider, settings_get().brightness, LV_ANIM_OFF);
  lv_obj_add_event_cb(s_slider, slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(s_slider, slider_cb, LV_EVENT_RELEASED, nullptr);

  lv_obj_t* btn = lv_button_create(t);
  lv_obj_set_size(btn, 124, 34);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, -8);
  lv_obj_set_style_radius(btn, 17, 0);
  lv_obj_add_event_cb(btn, refresh_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bl = lv_label_create(btn);
  lv_label_set_text(bl, LV_SYMBOL_REFRESH "  Refresh");
  lv_obj_center(bl);

  s_ip = mk_label(t, &lv_font_montserrat_12, C_MUTED, 22, "");
  s_err = mk_label(t, &lv_font_montserrat_12, lv_color_hex(0xF2384A), 38, "");
  lv_obj_set_width(s_err, 180);
  lv_label_set_long_mode(s_err, LV_LABEL_LONG_DOT);

  s_resetBtn = lv_button_create(t);
  lv_obj_set_size(s_resetBtn, 150, 26);
  lv_obj_align(s_resetBtn, LV_ALIGN_CENTER, 0, 62);
  lv_obj_set_style_radius(s_resetBtn, 13, 0);
  lv_obj_set_style_bg_color(s_resetBtn, lv_color_hex(0x3A1E24), 0);
  lv_obj_add_event_cb(s_resetBtn, reset_cb, LV_EVENT_LONG_PRESSED, nullptr);
  s_resetLbl = lv_label_create(s_resetBtn);
  lv_obj_set_style_text_font(s_resetLbl, &lv_font_montserrat_12, 0);
  lv_label_set_text(s_resetLbl, "Hold: reset WiFi");
  lv_obj_center(s_resetLbl);

  mk_label(t, &lv_font_montserrat_12, C_MUTED, 88, "aqicn.org | Open-Meteo");
}

static void settings_update() {
  if (st.phase == NetPhase::Online && st.ip[0])
    lv_label_set_text_fmt(s_ip, LV_SYMBOL_WIFI " %s", st.ip);
  else if (st.phase == NetPhase::SetupPortal)
    lv_label_set_text_fmt(s_ip, "Setup AP: %s", st.apName);
  else
    lv_label_set_text(s_ip, LV_SYMBOL_WIFI " offline");
  lv_label_set_text(s_err, st.lastError);
}

// ---------------------------------------------------------------------------
// Page dots, overlay, timers
// ---------------------------------------------------------------------------
static void update_dots() {
  for (int i = 0; i < TILE_COUNT; i++) {
    bool active = on_tile(i);
    lv_obj_set_size(dots[i], active ? 14 : 6, 6);
    lv_obj_set_style_bg_color(dots[i], active ? C_TEXT : C_MUTED, 0);
  }
}

static void tv_changed(lv_event_t*) {
  update_dots();
  if (s_resetArmedAt) {
    s_resetArmedAt = 0;
    lv_label_set_text(s_resetLbl, "Hold: reset WiFi");
  }
}

static void ov_click(lv_event_t*) {
  if (st.otaProgress >= 0) return;
  ov_dismissed = true;
  lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
}

static void overlay_update() {
  if (st.phase != ov_lastPhase) {
    ov_dismissed = false;
    ov_lastPhase = st.phase;
  }
  char msg[160] = "";
  if (st.otaProgress >= 0) {
    snprintf(msg, sizeof(msg), LV_SYMBOL_DOWNLOAD "\nUpdating firmware\n%d%%", st.otaProgress);
  } else if (st.phase == NetPhase::SetupPortal && !st.data.valid) {
    snprintf(msg, sizeof(msg), LV_SYMBOL_WIFI "\nWiFi setup\n\nJoin network\n%s\nthen open 192.168.4.1",
             st.apName);
  } else if (!st.data.valid) {
    if (st.phase == NetPhase::Online && st.lastError[0] && !st.fetching)
      snprintf(msg, sizeof(msg), LV_SYMBOL_WARNING "\nNo data yet\n\nCheck token and location at\nhttp://%s",
               st.ip);
    else if (st.phase == NetPhase::Online)
      snprintf(msg, sizeof(msg), LV_SYMBOL_REFRESH "\nFetching air quality...");
    else
      snprintf(msg, sizeof(msg), LV_SYMBOL_WIFI "\nConnecting to WiFi...");
  }

  if (msg[0] && (!ov_dismissed || st.otaProgress >= 0)) {
    lv_label_set_text(ov_lbl, msg);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
  }
}

static void tick_timer(lv_timer_t*) {
  home_update_fresh();
  if (s_resetArmedAt && millis() - s_resetArmedAt > 5000) {
    s_resetArmedAt = 0;
    lv_label_set_text(s_resetLbl, "Hold: reset WiFi");
  }
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
void ui_init() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  set_screen_tint(-1);
  lv_obj_set_style_text_color(scr, C_TEXT, 0);

  tv = lv_tileview_create(scr);
  lv_obj_set_size(tv, 240, 240);
  lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(tv, tv_changed, LV_EVENT_VALUE_CHANGED, nullptr);

  for (int i = 0; i < TILE_COUNT; i++) {
    lv_dir_t dir = (i == 0) ? LV_DIR_RIGHT : (i == TILE_COUNT - 1 ? LV_DIR_LEFT : LV_DIR_HOR);
    tiles[i] = lv_tileview_add_tile(tv, i, 0, dir);
    lv_obj_set_style_bg_opa(tiles[i], LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(tiles[i], LV_OBJ_FLAG_SCROLLABLE);
  }
  build_home(tiles[TILE_HOME]);
  build_poll(tiles[TILE_POLL]);
  build_trend(tiles[TILE_TREND]);
  build_health(tiles[TILE_HEALTH]);
  build_settings(tiles[TILE_SETTINGS]);

  // Page indicator lives on the top layer so it doesn't scroll with tiles
  lv_obj_t* row = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 80, 10);
  lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -9);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 5, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  for (int i = 0; i < TILE_COUNT; i++) {
    dots[i] = lv_obj_create(row);
    lv_obj_remove_style_all(dots[i]);
    lv_obj_set_style_bg_opa(dots[i], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dots[i], 3, 0);
    lv_obj_remove_flag(dots[i], LV_OBJ_FLAG_CLICKABLE);
  }
  update_dots();

  // Full-screen status overlay (setup instructions, OTA progress). Tap to dismiss.
  ov = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(ov);
  lv_obj_set_size(ov, 240, 240);
  lv_obj_set_style_radius(ov, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(ov, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
  lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(ov, ov_click, LV_EVENT_CLICKED, nullptr);
  ov_lbl = mk_label(ov, &lv_font_montserrat_16, C_TEXT, 0, "Starting...");
  lv_obj_set_width(ov_lbl, 190);
  lv_label_set_long_mode(ov_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_center(ov_lbl);

  lv_timer_create(tick_timer, 10000, nullptr);
  lv_timer_create(poll_cycle_timer, 3500, nullptr);
  home_update_fresh();
}

void ui_apply_state(const AppState& s) {
  st = s;
  haveState = true;

  // Re-render data views only when a new reading arrived
  if (st.data.valid && st.data.fetched != lastDataStamp) {
    lastDataStamp = st.data.fetched;
    home_update();
    poll_update();
    trend_update();
    health_update();
  }
  home_update_fresh();
  settings_update();
  overlay_update();
}
