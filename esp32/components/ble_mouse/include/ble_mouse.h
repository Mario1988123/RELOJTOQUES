#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inicia el BLE simple (solo advertising y conexión).
 * device_name: nombre que verás en el móvil/PC (ej. "S3Watch Mouse").
 */
esp_err_t ble_mouse_start(const char *device_name);

#ifdef __cplusplus
}
#endif
