#pragma once
#include <stdint.h>

// Minimal CST816S driver on the shared Wire bus.
// Call only from the LVGL/loop task (Wire is not shared across tasks here).
bool touch_begin();
bool touch_read(uint16_t& x, uint16_t& y);  // true while a finger is down
