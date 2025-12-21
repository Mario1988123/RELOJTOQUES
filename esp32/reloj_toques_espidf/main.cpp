#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "display_manager.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"

#include "lvgl.h"
#include "sensors.h"
#include "settings.h"
#include "ui.h"

// HID Combinado (Mouse + Keyboard en un solo dispositivo)
#include "ble_hid_combined.h"

// Power management
#include "esp_wifi.h"
#include "esp_bt.h"
#include "esp_sleep.h"
#include "esp_pm.h"

#include "audio_alert.h"

// ⚠️ NVS para BLE
#include "nvs_flash.h"

static const char *TAG = "MAIN";

/* --------------------------------------------------------
   NVS INIT – necesario antes de usar Bluetooth
   -------------------------------------------------------- */
static void nvs_init_safe(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }
}

/* --------------------------------------------------------
   POWER INIT – BLE only + sin WiFi + sin BT clásico
   -------------------------------------------------------- */
static void power_init(void) {
    // WiFi completamente apagado
    esp_wifi_stop();
    esp_wifi_deinit();

    // Liberar memoria del Bluetooth clásico
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
}

/* ========================================================
   APP_MAIN
   ======================================================== */
extern "C" void app_main(void) {

    // 1) NVS PRIMERO (BLE lo necesita)
    nvs_init_safe();

    // 2) Config de energía
    power_init();

    // 3) Event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 4) Activar pantalla pronto
    display_manager_pm_early_init();

    // 5) Iniciar pantalla / BSP
    bsp_display_start();
    bsp_extra_init();

    // 6) Cargar ajustes del usuario
    settings_init();

    /* --------------------------------------------------------
       HID COMBINADO BLE (Mouse + Keyboard)
       Emparejamiento con PIN: 1234
       -------------------------------------------------------- */

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Iniciando BLE HID Combinado");
    ESP_LOGI(TAG, "  Dispositivo: S3Watch HID");
    ESP_LOGI(TAG, "  Modo: SIN PIN (Just Works)");
    ESP_LOGI(TAG, "========================================");

    // SIN PIN para que sea más simple
    esp_err_t hid_err = ble_hid_combined_init("S3Watch HID", false);

    if (hid_err != ESP_OK) {
        ESP_LOGE(TAG, "✗ BLE HID init FAILED: %s", esp_err_to_name(hid_err));
    } else {
        ESP_LOGI(TAG, "✓ BLE HID READY!");
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, ">>> PARA EMPAREJAR:");
        ESP_LOGI(TAG, ">>> 1. Abre Bluetooth en tu móvil");
        ESP_LOGI(TAG, ">>> 2. Busca: 'S3Watch HID'");
        ESP_LOGI(TAG, ">>> 3. Pulsa emparejar (SIN PIN)");
        ESP_LOGI(TAG, "");
    }

    /* --------------------------------------------------------
       UI
       -------------------------------------------------------- */

    // Tarea principal de LVGL
    xTaskCreate(ui_task, "ui", 8000, NULL, 4, NULL);

    // Sonido de inicio
    audio_alert_play_startup();

    /* --------------------------------------------------------
       POWER MANAGEMENT — NO APAGAR NUNCA
       -------------------------------------------------------- */

    esp_pm_config_t pm_cfg = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 240,
        .light_sleep_enable = false
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_cfg));

    // Sin fuentes de wakeup (por seguridad)
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    ESP_LOGI(TAG, "Screen will NEVER turn off. PM disabled.");
}
