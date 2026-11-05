#include "imu_qmi8658.h"
#include <Arduino.h>
#include <Wire.h>

static uint8_t s_addr = 0;

static constexpr uint8_t REG_WHO_AM_I = 0x00;  // reads 0x05
static constexpr uint8_t REG_CTRL1 = 0x02;
static constexpr uint8_t REG_CTRL2 = 0x03;     // accel full-scale + ODR
static constexpr uint8_t REG_CTRL7 = 0x08;     // sensor enable
static constexpr uint8_t REG_AX_L = 0x35;

static bool wr(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool rd(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

bool imu_begin() {
  const uint8_t candidates[] = {0x6B, 0x6A};
  for (uint8_t a : candidates) {
    uint8_t id = 0;
    if (rd(a, REG_WHO_AM_I, &id, 1) && id == 0x05) {
      s_addr = a;
      break;
    }
  }
  if (!s_addr) return false;

  wr(REG_CTRL1, 0x40);  // register address auto-increment, little endian
  wr(REG_CTRL2, 0x15);  // accel +/-4 g, ~250 Hz
  wr(REG_CTRL7, 0x01);  // accelerometer on, gyro off (saves power)
  delay(20);
  return true;
}

bool imu_read_accel(float& ax, float& ay, float& az) {
  if (!s_addr) return false;
  uint8_t b[6];
  if (!rd(s_addr, REG_AX_L, b, 6)) return false;
  const float lsbPerG = 8192.0f;  // +/-4 g range
  ax = (int16_t)((b[1] << 8) | b[0]) / lsbPerG;
  ay = (int16_t)((b[3] << 8) | b[2]) / lsbPerG;
  az = (int16_t)((b[5] << 8) | b[4]) / lsbPerG;
  return true;
}
