#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa el HID Keyboard Bluetooth
 * @param device_name Nombre del dispositivo visible
 * @return ESP_OK en caso de éxito
 */
esp_err_t ble_hid_keyboard_init(const char *device_name);

/**
 * @brief Verifica si el teclado HID está conectado
 * @return true si está conectado
 */
bool ble_hid_keyboard_is_connected(void);

/**
 * @brief Envía una cadena de texto al host
 * @param text Texto a enviar (solo ASCII)
 */
void ble_hid_keyboard_send_text(const char *text);

/**
 * @brief Envía una tecla especial (Enter, Backspace, etc.)
 * @param keycode Código de tecla HID
 */
void ble_hid_keyboard_send_key(uint8_t keycode);

// Códigos de teclas especiales HID
#define HID_KEY_ENTER       0x28
#define HID_KEY_BACKSPACE   0x2A
#define HID_KEY_TAB         0x2B
#define HID_KEY_SPACE       0x2C

#ifdef __cplusplus
}
#endif
