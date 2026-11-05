#pragma once
// Waveshare ESP32-S3-Touch-LCD-1.28 (touch version) pin map.
// Every value can be overridden from platformio.ini build_flags (-DNAME=value).

#ifndef PIN_LCD_SCLK
#define PIN_LCD_SCLK 10
#endif
#ifndef PIN_LCD_MOSI
#define PIN_LCD_MOSI 11
#endif
#ifndef PIN_LCD_CS
#define PIN_LCD_CS 9
#endif
#ifndef PIN_LCD_DC
#define PIN_LCD_DC 8
#endif
#ifndef PIN_LCD_RST
#define PIN_LCD_RST 14
#endif
#ifndef PIN_LCD_BL
#define PIN_LCD_BL 2
#endif

// Touch (CST816S) and IMU (QMI8658) share one I2C bus
#ifndef PIN_I2C_SDA
#define PIN_I2C_SDA 6
#endif
#ifndef PIN_I2C_SCL
#define PIN_I2C_SCL 7
#endif
#ifndef PIN_TP_INT
#define PIN_TP_INT 5
#endif
#ifndef PIN_TP_RST
#define PIN_TP_RST 13
#endif

// Backlight PWM frequency. Duty changes only dim the backlight if its driver
// follows this frequency; test others live with the serial command "blf <hz>".
#ifndef LCD_BL_FREQ
#define LCD_BL_FREQ 200
#endif

#define LCD_W 240
#define LCD_H 240

// Display tuning. If colours look wrong, flip LCD_INVERT or LCD_RGB_ORDER.
// If you see noise/glitches, drop LCD_SPI_HZ to 40000000.
#ifndef LCD_SPI_HZ
#define LCD_SPI_HZ 80000000
#endif
#ifndef LCD_INVERT
#define LCD_INVERT true
#endif
#ifndef LCD_RGB_ORDER
#define LCD_RGB_ORDER false
#endif

// Touch orientation fixes, in case taps land mirrored on your unit.
#ifndef TOUCH_SWAP_XY
#define TOUCH_SWAP_XY 0
#endif
#ifndef TOUCH_INVERT_X
#define TOUCH_INVERT_X 0
#endif
#ifndef TOUCH_INVERT_Y
#define TOUCH_INVERT_Y 0
#endif
