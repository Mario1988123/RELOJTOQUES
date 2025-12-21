#include "ble_sync.h"
#include "esp_log.h"
#include "esp_err.h"
#include <stdbool.h>

static const char *TAG = "BLE_SYNC_STUB";

// Define el EVENT_BASE que pide ui.c
ESP_EVENT_DEFINE_BASE(BLE_SYNC_EVENT_BASE);

/**
 * Stub de inicialización de ble_sync.
 */
esp_err_t ble_sync_init(void)
{
    ESP_LOGI(TAG, "ble_sync stub init");
    return ESP_OK;
}

/**
 * Stub para activar/desactivar ble_sync (sin lógica real).
 */
esp_err_t ble_sync_set_enabled(bool enabled)
{
    ESP_LOGI(TAG, "ble_sync stub set_enabled(%d)", enabled);
    (void)enabled;
    return ESP_OK;
}

/**
 * Stub que siempre dice que ble_sync está desactivado.
 */
bool ble_sync_is_enabled(void)
{
    return false;
}

/**
 * Stub de ble_sync_send_status con la MISMA firma
 * que en ble_sync.h: (int battery_percent, bool charging)
 */
esp_err_t ble_sync_send_status(int battery_percent, bool charging)
{
    ESP_LOGD(TAG,
             "ble_sync stub send_status batt=%d%% charging=%d",
             battery_percent, (int)charging);
    (void)battery_percent;
    (void)charging;
    return ESP_OK;
}
