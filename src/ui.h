#pragma once
#include "app_state.h"

// LVGL UI. All functions must be called from the loop task.
void ui_init();
void ui_apply_state(const AppState& st);
