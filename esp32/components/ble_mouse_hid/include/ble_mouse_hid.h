#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Inicializa el HID Mouse Bluetooth clásico.
// device_name: nombre que verá el móvil/PC ("S3Watch Mouse", etc.)
esp_err_t ble_hid_mouse_init(const char *device_name);

// Devuelve true si el HID está conectado
bool ble_hid_mouse_is_connected(void);

// Mover ratón: dx, dy, rueda (wheel)
void ble_hid_mouse_move(int8_t dx, int8_t dy, int8_t wheel);

// Botones: true = pulsado
void ble_hid_mouse_buttons(bool left, bool right, bool middle);

#ifdef __cplusplus
}
#endif
