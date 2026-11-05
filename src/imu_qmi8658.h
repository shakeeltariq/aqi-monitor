#pragma once

// Minimal QMI8658 accelerometer reader, used to wake the screen when the
// device is picked up or nudged. Call only from the loop task.
bool imu_begin();
bool imu_read_accel(float& ax, float& ay, float& az);  // in g
