#include "touch_cst816.h"
#include <Arduino.h>
#include <Wire.h>
#include "pins.h"

static constexpr uint8_t CST816_ADDR = 0x15;
static constexpr uint8_t REG_FINGERS = 0x02;    // finger count, then XH XL YH YL
static constexpr uint8_t REG_DIS_AUTOSLEEP = 0xFE;

bool touch_begin() {
  pinMode(PIN_TP_INT, INPUT_PULLUP);
  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW);
  delay(10);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(60);

  Wire.beginTransmission(CST816_ADDR);
  bool present = (Wire.endTransmission() == 0);

  // Keep the controller awake so polling always gets an answer.
  // If this write fails the chip still wakes on touch; polling just NACKs while idle.
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(REG_DIS_AUTOSLEEP);
  Wire.write(0x01);
  Wire.endTransmission();

  return present;
}

bool touch_read(uint16_t& x, uint16_t& y) {
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(REG_FINGERS);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)CST816_ADDR, (uint8_t)5) != 5) return false;

  uint8_t b[5];
  for (int i = 0; i < 5; i++) b[i] = Wire.read();

  if ((b[0] & 0x0F) == 0) return false;

  uint16_t tx = ((uint16_t)(b[1] & 0x0F) << 8) | b[2];
  uint16_t ty = ((uint16_t)(b[3] & 0x0F) << 8) | b[4];

#if TOUCH_SWAP_XY
  uint16_t t = tx; tx = ty; ty = t;
#endif
#if TOUCH_INVERT_X
  tx = (LCD_W - 1) - tx;
#endif
#if TOUCH_INVERT_Y
  ty = (LCD_H - 1) - ty;
#endif

  x = tx > (LCD_W - 1) ? (LCD_W - 1) : tx;
  y = ty > (LCD_H - 1) ? (LCD_H - 1) : ty;
  return true;
}
