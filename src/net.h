#pragma once
#include "app_state.h"

// Network task (core 0): WiFi + setup portal, NTP, AQI fetching, OTA, MQTT.
void net_start();

// Non-blocking. Returns true and fills `out` when a new state is available.
bool net_poll_state(AppState& out);

void net_request_refresh();
void net_request_wifi_reset();   // erases WiFi credentials and reboots into setup
