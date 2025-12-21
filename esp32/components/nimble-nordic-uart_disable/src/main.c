#include "nimble-nordic-uart.h"

#include <esp_log.h>
#include <esp_nimble_hci.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <nvs_flash.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

static const char *_TAG = "NORDIC UART";

// Declaración de la función interna de bajo nivel
// (implementada en nimble.c)
esp_err_t _nordic_uart_send(const char *message);

// Enviar texto por el servicio Nordic UART
esp_err_t nordic_uart_send(const char *message)
{
    return _nordic_uart_send(message);
}

esp_err_t nordic_uart_sendln(const char *message)
{
    if (nordic_uart_send(message) != ESP_OK) {
        return ESP_FAIL;
    }
    if (nordic_uart_send("\r\n") != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

/*
 * IMPORTANTE:
 *  - NO definimos aquí nordic_uart_start() ni nordic_uart_stop().
 *  - Esas funciones ya están implementadas en nimble.c.
 *  - Sólo deben existir allí (y estar declaradas en nimble-nordic-uart.h).
 *
 *  Esto evita:
 *    - multiple definition of `nordic_uart_start` / `nordic_uart_stop`
 *    - undefined reference to `_nordic_uart_start`
 */
