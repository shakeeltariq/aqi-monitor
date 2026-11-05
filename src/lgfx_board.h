#pragma once
// LovyanGFX driver for the GC9A01 round panel + PWM backlight.
// Touch is handled separately (touch_cst816.cpp) so the I2C bus can be shared
// with the IMU through the Arduino Wire driver.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "pins.h"

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

 public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = LCD_SPI_HZ;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_LCD_SCLK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc = PIN_LCD_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = PIN_LCD_CS;
      cfg.pin_rst = PIN_LCD_RST;
      cfg.pin_busy = -1;
      cfg.memory_width = LCD_W;
      cfg.memory_height = LCD_H;
      cfg.panel_width = LCD_W;
      cfg.panel_height = LCD_H;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = LCD_INVERT;
      cfg.rgb_order = LCD_RGB_ORDER;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = PIN_LCD_BL;
      cfg.invert = false;
      cfg.freq = LCD_BL_FREQ;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }

  // Re-initialise the backlight PWM at a new frequency (diagnostics).
  bool setBacklightFreq(uint32_t hz) {
    auto cfg = _light.config();
    cfg.freq = hz;
    _light.config(cfg);
    return _light.init(getBrightness());
  }
};
