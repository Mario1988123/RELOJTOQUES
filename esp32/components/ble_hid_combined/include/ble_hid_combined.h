#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa el dispositivo HID combinado (Mouse + Keyboard)
 * @param device_name Nombre del dispositivo BLE
 * @param use_pin Si true, requiere PIN 1234 para emparejar
 * @return ESP_OK en caso de éxito
 */
esp_err_t ble_hid_combined_init(const char *device_name, bool use_pin);

/**
 * @brief Verifica si el HID está conectado
 */
bool ble_hid_combined_is_connected(void);

/**
 * @brief Envía movimiento del mouse
 */
void ble_hid_mouse_move(int8_t dx, int8_t dy, int8_t wheel);

/**
 * @brief Envía botones del mouse
 */
void ble_hid_mouse_buttons(bool left, bool right, bool middle);

/**
 * @brief Envía texto via teclado
 */
void ble_hid_keyboard_send_text(const char *text);

/**
 * @brief Envía una tecla especial
 */
void ble_hid_keyboard_send_key(uint8_t keycode);

// Códigos de teclas HID
#define HID_KEY_ENTER       0x28
#define HID_KEY_BACKSPACE   0x2A
#define HID_KEY_TAB         0x2B
#define HID_KEY_SPACE       0x2C

#ifdef __cplusplus
}
#endif
