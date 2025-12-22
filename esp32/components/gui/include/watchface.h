#pragma once
#include "lvgl.h"
#include <stdbool.h>

void watchface_create(lv_obj_t *parent);
lv_obj_t *watchface_screen_get(void);    // <-- AÑADIR ESTA LÍNEA
void watchface_set_power_state(bool vbus_in, bool charging, int battery_percent);
void watchface_set_ble_connected(bool connected);
