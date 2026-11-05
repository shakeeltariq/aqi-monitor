# AQI Monitor: Waveshare ESP32-S3-Touch-LCD-1.28

A standalone air-quality display for the 1.28" round touch board. It shows measured
station AQI from WAQI (aqicn.org), with an Open-Meteo / CAMS model as the trend
source and as a fallback, in a swipeable LVGL 9 interface.

## Screens (swipe left/right)

| # | Screen | What it shows |
|---|--------|---------------|
| 1 | Home | Animated US AQI gauge, category, dominant pollutant, temp/humidity, station name, data time with a freshness dot. **Long-press** to refresh. |
| 2 | Pollutants | Ring of PM2.5 / PM10 / O3 / NO2 / SO2 / CO sub-indices. Tap a segment (or the centre) to inspect; auto-cycles when idle. |
| 3 | Trend | Last 12 h and next 12 h (model), plus tomorrow's peak chip. |
| 4 | Health | Advice for the current AQI band. |
| 5 | Settings | Brightness, Refresh, device IP, last error, hold-to-reset WiFi. |

The screen background tints with the AQI band colour. Values above 500 display as
"Hazardous+" with the real number shown.

Freshness dot: green = reading under 2 h old, amber = 2-4 h, red = older or offline.
"Station" means a WAQI measurement, "Model" means the Open-Meteo fallback is in use.

## What you need

1. The board, a USB-C data cable.
2. [VS Code](https://code.visualstudio.com/) + the **PlatformIO** extension (or `pip install platformio`).
3. A free WAQI token: <https://aqicn.org/data-platform/token/> (takes a minute, arrives by email).

## Build and flash

```bash
cd aqi-monitor
pio run -t upload        # first build downloads the toolchain and libraries
pio device monitor       # optional: serial log at 115200
```

If upload fails to connect: hold **BOOT**, tap **RESET**, release **BOOT**, then upload again.

After the first USB flash, later updates can go over WiFi:

```bash
pio run -e ota -t upload   # uses aqi-monitor.local
```

## First-time setup

1. On first boot the screen shows **WiFi setup** and the device opens an access point
   named `AQI-Setup-XXXX`.
2. Join it from your phone. The setup page opens automatically (or browse to `192.168.4.1`).
3. **Configure WiFi** → pick your network and password → Save. The device connects.
4. On your home WiFi, open **http://aqi-monitor.local** (or the IP shown on the Settings
   screen) → **Setup** and fill in:
   - **WAQI token** (required for station data; without it only the model is shown)
   - **Station**: use the built-in *Find a WAQI station* search, tap a result, and it fills
     `@<uid>`. Leave blank to use the station nearest your latitude/longitude.
   - **Latitude / Longitude**: your location (used for Open-Meteo and nearest-station lookup).
     The default is central Lahore.
   - Refresh interval, night dimming hours, screen timeout, timezone (`PKT-5` for Pakistan).
5. Save. The device fetches immediately.

To change WiFi later: Settings screen → long-press **Hold: reset WiFi** twice within
5 seconds. The device reboots into setup mode.

### Choosing a station in Lahore

Pick a fixed station rather than relying on "nearest". The nearest station can change
between fetches as stations go offline, which makes readings jump. Station search is
done from your browser directly to WAQI; if it fails, find the station on the map at
aqicn.org, open it, and copy its `@uid` from the station's API/widget details.

## Home Assistant (optional)

Fill in the MQTT host/port/user/password on the Setup page. The device publishes
Home Assistant discovery, so a device called **AQI Monitor** appears with sensors:
AQI, PM2.5/PM10/Ozone/NO2 sub-index, tomorrow's peak, dominant pollutant, station and
data source. State topic: `aqimon/<device-id>/state` (retained JSON).

## Data sources and their limits

- **WAQI (aqicn.org)** is the headline number. It aggregates real monitoring stations
  and reports US EPA AQI. Per-pollutant values in its `iaqi` block are **AQI sub-indices,
  not µg/m³**, and the screens label them that way. Free for non-commercial use with a
  token.
- **Open-Meteo Air Quality** (CAMS model, ~45 km grid outside Europe) supplies the 24 h
  trend, tomorrow's peak when WAQI has no forecast, and the headline when WAQI fails.
  Coarse model data can read well below station values during Lahore smog episodes,
  which is why it is not the primary source. No key needed.
- Attribution to both sources appears on the Settings screen. Keep it if you share
  the project.

## Configuration in code

`src/pins.h` holds the pin map and display/touch tuning, all overridable via
`build_flags` in `platformio.ini`:

| Symptom | Fix |
|---------|-----|
| Colours inverted or red/blue swapped | Flip `LCD_INVERT` or `LCD_RGB_ORDER` |
| Noise or glitches on screen | `-DLCD_SPI_HZ=40000000` |
| Taps land mirrored or rotated | `TOUCH_INVERT_X`, `TOUCH_INVERT_Y`, `TOUCH_SWAP_XY` |
| Touch not detected (serial log says MISSING) | Check `PIN_TP_RST` / I2C pins against your board revision |

AQI band colours, names and health tips are in `src/aqi_scale.cpp`.

## Architecture

```
Core 1  Arduino loop ── LVGL 9 (ui.cpp) ── LovyanGFX GC9A01 driver
                     ├─ CST816S touch + QMI8658 IMU (shared I2C via Wire)
                     └─ backlight: brightness, night dim, idle timeout, wake on touch/motion
           ▲  1-slot queue of AppState snapshots (no shared LVGL access)
Core 0  net task ─── WiFiManager (setup AP + settings web page)
                  ├─ HTTPS: WAQI feed + Open-Meteo, streamed & filtered JSON (ArduinoJson 7)
                  ├─ retry with exponential backoff, keeps last good data on failure
                  ├─ NTP time, ArduinoOTA, mDNS (aqi-monitor.local)
                  └─ MQTT + Home Assistant discovery (optional)
```

Memory: LVGL renders into two 240×40 partial buffers in internal DMA RAM (~38 KB total);
larger allocations go to the 2 MB PSRAM, which leaves internal heap for TLS.

## Security notes

- HTTPS certificates are **not verified** (`setInsecure()`). The data is public, but your
  WAQI token could be read by someone able to intercept your traffic. To harden this, pin
  the root CA for `api.waqi.info` and `air-quality-api.open-meteo.com` with
  `client.setCACert(...)`.
- OTA has no password. Add `ArduinoOTA.setPassword("...")` in `net.cpp` (and
  `upload_flags = --auth=...` in `platformio.ini`) if your LAN isn't trusted.
- The settings page is reachable by anyone on your LAN.

## Build status

The UI code was compiled and rendered against LVGL 9.2.2 in a desktop simulator, and the
screen layouts were checked visually. The complete ESP32 firmware build (`pio run`)
has not yet been run, because the toolchain couldn't be downloaded in the environment
where this was written. Run it once on your machine and report any compile errors.

## File map

```
platformio.ini        board, PSRAM/flash, libraries, OTA env
include/lv_conf.h     LVGL configuration
src/main.cpp          setup/loop, LVGL glue, backlight, IMU wake
src/ui.cpp            all screens
src/net.cpp           WiFi, setup page, fetching, OTA, MQTT
src/settings.cpp      NVS-backed settings
src/aqi_scale.cpp     AQI bands, colours, tips
src/touch_cst816.cpp  touch driver
src/imu_qmi8658.cpp   accelerometer
src/lgfx_board.h      display driver config
src/pins.h            pin map + tuning
```
