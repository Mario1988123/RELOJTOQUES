#include "ble_hid_keyboard.h"
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Núcleo Bluetooth
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"

// HID de Espressif
#include "esp_hidd.h"
#include "esp_hid_common.h"

static const char *TAG = "BLE_HID_KEYBOARD";

/* Estado interno */
static bool s_ble_started = false;
static bool s_connected = false;
static char s_device_name[32] = {0};
static esp_hidd_dev_t *s_hid_dev = NULL;

/* HID Report Descriptor para teclado estándar */
static const uint8_t s_keyboard_report_map[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x05, 0x07,       //   Usage Page (Key Codes)
    0x19, 0xE0,       //   Usage Minimum (Keyboard LeftControl)
    0x29, 0xE7,       //   Usage Maximum (Keyboard Right GUI)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8) - modifier keys
    0x81, 0x02,       //   Input (Data,Var,Abs) - modifier byte
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x01,       //   Input (Const) - reserved byte
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x65,       //   Logical Maximum (101)
    0x05, 0x07,       //   Usage Page (Key Codes)
    0x19, 0x00,       //   Usage Minimum (Reserved)
    0x29, 0x65,       //   Usage Maximum (Keyboard Application)
    0x81, 0x00,       //   Input (Data,Array) - key array (6 keys)
    0xC0              // End Collection
};

static esp_hid_raw_report_map_t s_report_maps[] = {
    {
        .data = s_keyboard_report_map,
        .len = sizeof(s_keyboard_report_map),
    }
};

static const esp_hid_device_config_t s_hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05E0,
    .version = 0x0100,
    .device_name = "S3Watch Keyboard",
    .manufacturer_name = "Pablo",
    .serial_number = "0002",
    .report_maps = s_report_maps,
    .report_maps_len = 1,
};

/* Advertising BLE */
static esp_ble_adv_data_t s_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x20,
    .max_interval = 0x40,
    .appearance = 0x03C1,  // Keyboard HID
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0,
    .p_service_uuid = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

/* Callbacks HID */
static void hid_event_handler(void *handler_arg,
                              esp_event_base_t base,
                              int32_t event_id,
                              void *event_data)
{
    (void)handler_arg;
    (void)base;
    esp_hidd_event_t ev = (esp_hidd_event_t)event_id;
    esp_hidd_event_data_t *ed = (esp_hidd_event_data_t *)event_data;

    switch (ev) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "HID Keyboard start - iniciando advertising");
        esp_ble_gap_config_adv_data(&s_adv_data);
        break;

    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "HID Keyboard CONNECT");
        s_connected = true;
        break;

    case ESP_HIDD_DISCONNECT_EVENT:
        ESP_LOGW(TAG, "HID Keyboard DISCONNECT: reason=%d", ed->disconnect.reason);
        s_connected = false;
        esp_ble_gap_start_advertising(&s_adv_params);
        break;

    default:
        break;
    }
}

/* GAP callback */
static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "ADV data configurada, arrancando advertising");
        esp_ble_gap_start_advertising(&s_adv_params);
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Advertising BLE iniciado");
        } else {
            ESP_LOGE(TAG, "Fallo al iniciar advertising, status=0x%02X",
                     param->adv_start_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (param->ble_security.auth_cmpl.success) {
            ESP_LOGI(TAG, "Emparejamiento completado");
            s_connected = true;
        } else {
            ESP_LOGW(TAG, "Emparejamiento FALLÓ, status=0x%02X",
                     param->ble_security.auth_cmpl.fail_reason);
            s_connected = false;
            esp_ble_gap_start_advertising(&s_adv_params);
        }
        break;

    default:
        break;
    }
}

/* API pública */
esp_err_t ble_hid_keyboard_init(const char *device_name)
{
    if (device_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Guardar nombre
    size_t i = 0;
    while (device_name[i] != '\0' && i < sizeof(s_device_name) - 1) {
        s_device_name[i] = device_name[i];
        i++;
    }
    s_device_name[i] = '\0';

    if (!s_ble_started) {
        ESP_LOGI(TAG, "Inicializando controlador BT en modo BLE");
        esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
        ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

        ESP_ERROR_CHECK(esp_bluedroid_init());
        ESP_ERROR_CHECK(esp_bluedroid_enable());
        ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));

        s_ble_started = true;
    }

    ESP_ERROR_CHECK(esp_ble_gap_set_device_name(s_device_name));

    // Seguridad: Just Works
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_ONLY | ESP_LE_AUTH_BOND;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t resp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;

    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &resp_key, sizeof(resp_key));

    // Inicializar dispositivo HID
    ESP_ERROR_CHECK(esp_hidd_dev_init(&s_hid_config,
                                      ESP_HID_TRANSPORT_BLE,
                                      hid_event_handler,
                                      &s_hid_dev));

    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&s_adv_data));

    ESP_LOGI(TAG, "BLE Keyboard inicializado: \"%s\"", s_device_name);

    s_connected = false;
    return ESP_OK;
}

bool ble_hid_keyboard_is_connected(void)
{
    if (!s_hid_dev) {
        return false;
    }
    return esp_hidd_dev_connected(s_hid_dev);
}

/* Tabla de conversión ASCII a HID Keyboard */
static const uint8_t ascii_to_hid[128] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // 0x00-0x07
    0x2A, 0x2B, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00,  // 0x08-0x0F (Backspace, Tab, Enter)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // 0x10-0x17
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // 0x18-0x1F
    0x2C, 0x1E, 0x34, 0x20, 0x21, 0x22, 0x24, 0x34,  // 0x20-0x27 (Space, !, ", #, $, %, &, ')
    0x26, 0x27, 0x25, 0x2E, 0x36, 0x2D, 0x37, 0x38,  // 0x28-0x2F ((, ), *, +, ',', -, ., /)
    0x27, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24,  // 0x30-0x37 (0-7)
    0x25, 0x26, 0x33, 0x33, 0x36, 0x2E, 0x37, 0x38,  // 0x38-0x3F (8-9, :, ;, <, =, >, ?)
    0x1F, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,  // 0x40-0x47 (@, A-G)
    0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,  // 0x48-0x4F (H-O)
    0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A,  // 0x50-0x57 (P-W)
    0x1B, 0x1C, 0x1D, 0x2F, 0x31, 0x30, 0x23, 0x2D,  // 0x58-0x5F (X-Z, [, \, ], ^, _)
    0x35, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,  // 0x60-0x67 (`, a-g)
    0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,  // 0x68-0x6F (h-o)
    0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A,  // 0x70-0x77 (p-w)
    0x1B, 0x1C, 0x1D, 0x2F, 0x31, 0x30, 0x35, 0x00   // 0x78-0x7F (x-z, {, |, }, ~, DEL)
};

/* Enviar reporte de teclado */
static void send_keyboard_report(uint8_t modifier, uint8_t keycode)
{
    if (!s_hid_dev || !esp_hidd_dev_connected(s_hid_dev)) {
        return;
    }

    uint8_t report[8] = {modifier, 0x00, keycode, 0x00, 0x00, 0x00, 0x00, 0x00};

    esp_err_t err = esp_hidd_dev_input_set(s_hid_dev, 0, 0, report, sizeof(report));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Fallo al enviar reporte: %s", esp_err_to_name(err));
    }

    // Release key
    vTaskDelay(pdMS_TO_TICKS(10));
    memset(report, 0, sizeof(report));
    esp_hidd_dev_input_set(s_hid_dev, 0, 0, report, sizeof(report));
    vTaskDelay(pdMS_TO_TICKS(10));
}

void ble_hid_keyboard_send_key(uint8_t keycode)
{
    send_keyboard_report(0x00, keycode);
}

void ble_hid_keyboard_send_text(const char *text)
{
    if (!text || !ble_hid_keyboard_is_connected()) {
        return;
    }

    for (size_t i = 0; text[i] != '\0'; i++) {
        uint8_t ch = (uint8_t)text[i];
        if (ch >= 128) continue;  // Solo ASCII

        uint8_t modifier = 0x00;
        uint8_t keycode = ascii_to_hid[ch];

        // Mayúsculas y símbolos requieren Shift
        if (ch >= 'A' && ch <= 'Z') {
            modifier = 0x02;  // Left Shift
        } else if (ch == '!' || ch == '@' || ch == '#' || ch == '$' ||
                   ch == '%' || ch == '^' || ch == '&' || ch == '*' ||
                   ch == '(' || ch == ')' || ch == '_' || ch == '+' ||
                   ch == '{' || ch == '}' || ch == '|' || ch == ':' ||
                   ch == '"' || ch == '<' || ch == '>' || ch == '?') {
            modifier = 0x02;  // Left Shift
        }

        if (keycode != 0x00) {
            send_keyboard_report(modifier, keycode);
        }
    }
}
