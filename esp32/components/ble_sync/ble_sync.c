#include "ble_sync.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "bsp_power.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "nimble-nordic-uart.h"     // BLE REAL
#include "rtc_lib.h"
#include "esp-bsp.h"
#include "sensors.h"
#include "esp_event.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "notifications.h"
#include "display_manager.h"
#include "ui.h"
#include "audio_alert.h"

typedef struct {
    char* ts; char* app; char* title; char* msg;
} notif_async_t;

static void notif_async_cb(void* p)
{
    notif_async_t* c = (notif_async_t*)p;
    ui_show_messages_tile();
    notifications_show(c->app, c->title, c->msg, c->ts);
    free(c->ts);
    free(c->app);
    free(c->title);
    free(c->msg);
    free(c);
}

static const char* TAG = "BLE_SYNC";

// BLE event base
ESP_EVENT_DEFINE_BASE(BLE_SYNC_EVENT_BASE);

// BLE states
static volatile bool s_ble_connected = false;
static TimerHandle_t s_status_timer = NULL;
static TimerHandle_t s_time_sync_timer = NULL;
static bool s_time_sync_requested = false;
static bool s_ble_enabled = false;
static bool s_ble_stack_started = false;

static void status_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    if (s_ble_connected) {
        ble_sync_send_status(bsp_power_get_battery_percent(), bsp_power_is_charging());
    }
}

static void time_sync_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    const char* sync_cmd = "{\"cmd\":\"time_sync\"}\n";
    nordic_uart_sendln(sync_cmd);
    ESP_LOGI(TAG, "Requested time sync on connect (delayed)");
}

static void handle_notification_fields(const char* timestamp,
                                       const char* app,
                                       const char* title,
                                       const char* message)
{
    ESP_LOGI(TAG, "Notification: app='%s' title='%s' msg='%s' ts='%s'",
             app, title, message, timestamp);

    display_manager_turn_on();

    bool locked = false;
    for (int i = 0; i < 3 && !locked; ++i) {
        locked = bsp_display_lock(150);
        if (!locked) vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (locked) {
        ui_show_messages_tile();
        notifications_show(app, title, message, timestamp);
        bsp_display_unlock();
    } else {
        notif_async_t* ctx = calloc(1, sizeof(notif_async_t));
        if (ctx) {
            ctx->ts    = timestamp ? strdup(timestamp) : NULL;
            ctx->app   = app       ? strdup(app)       : NULL;
            ctx->title = title     ? strdup(title)     : NULL;
            ctx->msg   = message   ? strdup(message)   : NULL;
            lv_async_call(notif_async_cb, ctx);
        }
    }

    audio_alert_notify();
}

static void process_one_json_object(const char* json, size_t len)
{
    char* tmp = malloc(len + 1);
    if (!tmp) return;

    memcpy(tmp, json, len);
    tmp[len] = '\0';

    cJSON* root = cJSON_Parse(tmp);
    if (!root) {
        free(tmp);
        return;
    }

    cJSON* datetime = cJSON_GetObjectItem(root, "datetime");
    if (cJSON_IsString(datetime)) {
        int y, m, d, h, mi, s;
        if (sscanf(datetime->valuestring, "%d-%d-%dT%d:%d:%d",
                   &y, &m, &d, &h, &mi, &s) == 6) {
            struct tm t = {
                .tm_year = y,
                .tm_mon  = m,
                .tm_mday = d,
                .tm_hour = h,
                .tm_min  = mi,
                .tm_sec  = s
            };
            rtc_set_time(&t);
        }
    }

    cJSON* notif = cJSON_GetObjectItem(root, "notification");
    if (cJSON_IsString(notif)) {
        handle_notification_fields(
            notif->valuestring,
            cJSON_GetObjectItem(root, "app")->valuestring,
            cJSON_GetObjectItem(root, "title")->valuestring,
            cJSON_GetObjectItem(root, "message")->valuestring);
    }

    cJSON* status = cJSON_GetObjectItem(root, "status");
    if (cJSON_IsString(status)) {
        ble_sync_send_status(bsp_power_get_battery_percent(), bsp_power_is_charging());
    }

    cJSON_Delete(root);
    free(tmp);
}

void uartTask(void* parameter)
{
    (void)parameter;
    static char mbuf[CONFIG_NORDIC_UART_MAX_LINE_LENGTH + 1];

    for (;;) {
        size_t item_size;
        const char* item = xRingbufferReceive(nordic_uart_rx_buf_handle,
                                              &item_size,
                                              portMAX_DELAY);

        if (item) {
            memcpy(mbuf, item, item_size);
            mbuf[item_size] = '\0';
            vRingbufferReturnItem(nordic_uart_rx_buf_handle, (void*)item);

            process_one_json_object(mbuf, item_size);
        }
    }
}

/* Callback con el tipo real que define nimble-nordic-uart.h:
 * enum nordic_uart_callback_type { NORDIC_UART_CONNECTED, NORDIC_UART_DISCONNECTED, ... }
 */
static void nordic_uart_callback(enum nordic_uart_callback_type type)
{
    if (type == NORDIC_UART_CONNECTED) {

        ESP_LOGI(TAG, "BLE CONNECTED");
        s_ble_connected = true;

        (void)esp_event_post(BLE_SYNC_EVENT_BASE, BLE_SYNC_EVT_CONNECTED, NULL, 0, 0);

        ble_sync_send_status(bsp_power_get_battery_percent(), bsp_power_is_charging());

        bool need_sync = false;

        int y = rtc_get_year();
        int m = rtc_get_month();
        int d = rtc_get_day();

        if (y <= 0 || m <= 0 || d <= 0) need_sync = true;

        if (need_sync && !s_time_sync_requested) {
            s_time_sync_requested = true;

            if (!s_time_sync_timer) {
                s_time_sync_timer = xTimerCreate("ble_sync_time",
                                                 pdMS_TO_TICKS(1500),
                                                 pdFALSE,
                                                 NULL,
                                                 time_sync_timer_cb);
            }
            if (s_time_sync_timer) {
                xTimerStart(s_time_sync_timer, 0);
            } else {
                time_sync_timer_cb(NULL);
            }
        }

    } else if (type == NORDIC_UART_DISCONNECTED) {
        ESP_LOGI(TAG, "BLE DISCONNECTED");
        s_ble_connected = false;
        s_time_sync_requested = false;
        (void)esp_event_post(BLE_SYNC_EVENT_BASE, BLE_SYNC_EVT_DISCONNECTED, NULL, 0, 0);
    }
}

static void power_ble_evt(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    (void)handler_arg;
    (void)base;
    (void)id;

    bsp_power_event_payload_t* pl = event_data;
    if (pl) {
        ble_sync_send_status(pl->battery_percent, pl->charging);
    }
}

esp_err_t ble_sync_init(void)
{
    esp_err_t err = ble_sync_set_enabled(true);
    if (err != ESP_OK) return err;

    xTaskCreate(uartTask, "uartTask", 4096, NULL, 3, NULL);

    if (!s_status_timer) {
        s_status_timer = xTimerCreate("status5m",
                                      pdMS_TO_TICKS(5 * 60 * 1000),
                                      pdTRUE,
                                      NULL,
                                      status_timer_cb);
        if (s_status_timer) xTimerStart(s_status_timer, 0);
    }

    esp_event_handler_register(BSP_POWER_EVENT_BASE, ESP_EVENT_ANY_ID, power_ble_evt, NULL);

    return ESP_OK;
}

esp_err_t ble_sync_send_status(int battery_percent, bool charging)
{
    if (!s_ble_enabled) return ESP_ERR_INVALID_STATE;

    cJSON* root = cJSON_CreateObject();
    if (!root) return ESP_FAIL;

    cJSON_AddNumberToObject(root, "battery", battery_percent);
    cJSON_AddBoolToObject(root, "charging", charging);
    cJSON_AddNumberToObject(root, "steps", sensors_get_step_count());

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json) return ESP_FAIL;

    esp_err_t err = nordic_uart_sendln(json);
    free(json);

    return err;
}

esp_err_t ble_sync_set_enabled(bool enabled)
{
    if (enabled == s_ble_enabled) return ESP_OK;

    if (enabled) {

        if (!s_ble_stack_started) {

            esp_err_t err = nordic_uart_start("ESP32 S3 Watch", nordic_uart_callback);
            if (err != ESP_OK) return err;

            s_ble_stack_started = true;
        }

        s_ble_enabled = true;
        nordic_uart_set_advertising_enabled(true);

        return ESP_OK;
    }

    s_ble_enabled = false;
    s_ble_connected = false;
    nordic_uart_set_advertising_enabled(false);
    nordic_uart_disconnect();

    return ESP_OK;
}

bool ble_sync_is_enabled(void)
{
    return s_ble_enabled;
}
