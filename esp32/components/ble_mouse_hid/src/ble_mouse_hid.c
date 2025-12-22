#include "ble_mouse_hid.h"

#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_hidd_api.h"
#include "esp_gatt_common_api.h"
#include "nvs_flash.h"

static const char *TAG = "BLE_MOUSE_HID";

static uint8_t hid_report_map[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x02,       // Usage (Mouse)
    0xA1, 0x01,       // Collection (Application)
    0x09, 0x01,       //   Usage (Pointer)
    0xA1, 0x00,       //   Collection (Physical)
    0x05, 0x09,       //     Usage Page (Buttons)
    0x19, 0x01,       //     Usage Minimum (01)
    0x29, 0x03,       //     Usage Maximum (03)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x95, 0x03,       //     Report Count (3)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x02,       //     Input (Data,Var,Abs)
    0x95, 0x01,       //     Report Count (1)
    0x75, 0x05,       //     Report Size (5)
    0x81, 0x01,       //     Input (Const,Arr,Abs)  ; Padding
    0x05, 0x01,       //     Usage Page (Generic Desktop)
    0x09, 0x30,       //     Usage (X)
    0x09, 0x31,       //     Usage (Y)
    0x15, 0x81,       //     Logical Minimum (-127)
    0x25, 0x7F,       //     Logical Maximum (127)
    0x75, 0x08,       //     Report Size (8)
    0x95, 0x02,       //     Report Count (2)
    0x81, 0x06,       //     Input (Data,Var,Rel)
    0xC0,             //   End Collection
    0xC0              // End Collection
};

// Config HID
static esp_hidd_dev_t *s_hid_dev = NULL;
static bool s_is_connected = false;

// GAP callbacks (conexión, pairing, etc.)
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

// HID callbacks
static void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param);

esp_err_t ble_mouse_hid_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Init BLE HID mouse");

    // --- NVS para BT ---
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // --- BT Controller ---
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "%s init controller failed: %s", __func__, esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return ret;
    }

    // --- Bluedroid ---
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "%s bluedroid init failed: %s", __func__, esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "%s bluedroid enable failed: %s", __func__, esp_err_to_name(ret));
        return ret;
    }

    // --- Register GAP callback ---
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));

    // --- HID Device config ---
    esp_hidd_app_param_t app_param = {0};
    app_param.name = (uint8_t *)"S3Watch Mouse";
    app_param.description = (uint8_t *)"ESP32-S3 HID Mouse";
    app_param.provider = (uint8_t *)"Pablo";
    app_param.subclass = 0x40; // Mouse
    app_param.desc_list = hid_report_map;
    app_param.desc_list_len = sizeof(hid_report_map);

    esp_hidd_qos_param_t both_qos;
    memset(&both_qos, 0, sizeof(both_qos));

    ret = esp_hidd_dev_init(&app_param, &both_qos, &s_hid_dev);
    if (ret) {
        ESP_LOGE(TAG, "HID device init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_ERROR_CHECK(esp_hidd_dev_register_callbacks(hidd_event_callback));

    // --- Advertising ---
    esp_ble_adv_data_t adv_data = {
        .set_scan_rsp = false,
        .include_name = true,
        .include_txpower = true,
        .min_interval = 0x20,
        .max_interval = 0x40,
        .appearance = 962, // HID Mouse
        .manufacturer_len = 0,
        .p_manufacturer_data = NULL,
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = 0,
        .p_service_uuid = NULL,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
    };

    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&adv_data));

    // El evento ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT en gap_event_handler arrancará el advertising

    ESP_LOGI(TAG, "BLE HID mouse initialized; waiting for connections");
    return ESP_OK;
}

esp_err_t ble_mouse_move(int8_t dx, int8_t dy, uint8_t buttons)
{
    if (!s_is_connected || s_hid_dev == NULL) {
        return ESP_FAIL;
    }

    uint8_t report[3];
    report[0] = buttons; // botones
    report[1] = (uint8_t)dx;
    report[2] = (uint8_t)dy;

    esp_err_t ret = esp_hidd_dev_input_set(s_hid_dev, 0, report, sizeof(report));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send mouse report: %s", esp_err_to_name(ret));
    }
    return ret;
}

// ================== GAP events ===================

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {

    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT: {
        // Empezar a anunciarse
        esp_ble_adv_params_t adv_params = {
            .adv_int_min = 0x20,
            .adv_int_max = 0x40,
            .adv_type = ADV_TYPE_IND,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .channel_map = ADV_CHNL_ALL,
            .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        };
        ESP_LOGI(TAG, "Starting advertising");
        esp_ble_gap_start_advertising(&adv_params);
        break;
    }

    case ESP_GAP_BLE_SEC_REQ_EVT:
        // Siempre aceptar emparejamiento
        ESP_LOGI(TAG, "ESP_GAP_BLE_SEC_REQ_EVT, accept pairing");
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;

    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (!param->ble_security.auth_cmpl.success) {
            ESP_LOGE(TAG, "Pairing failed, reason = 0x%x",
                     param->ble_security.auth_cmpl.fail_reason);
        } else {
            ESP_LOGI(TAG, "Pairing success, bonded device");
        }
        break;

    case ESP_GAP_BLE_PASSKEY_REQ_EVT:
        // Usamos passkey fija 1234 (la configuramos abajo)
        ESP_LOGI(TAG, "Passkey request (phone espera código 1234)");
        break;

    default:
        break;
    }
}

// ================== HID callbacks ===================

static void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
    switch (event) {
    case ESP_HIDD_EVENT_REG_FINISH: {
        if (param->init_finish.state == ESP_HIDD_INIT_OK) {
            ESP_LOGI(TAG, "HID registration complete");
            // Nada más, GAP ya configurado
        } else {
            ESP_LOGE(TAG, "HID registration failed");
        }
        break;
    }
    case ESP_HIDD_EVENT_BLE_CONNECT: {
        ESP_LOGI(TAG, "HID connected");
        s_is_connected = true;

        // Configurar seguridad y passkey 1234 aquí:
        uint32_t passkey = 1234;
        esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_MITM_BOND;
        esp_ble_io_cap_t iocap = ESP_IO_CAP_OUT; // Mostramos passkey en móvil
        uint8_t key_size = 16;
        uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
        uint8_t rsp_key  = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

        esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));
        esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
        esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));
        esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key));
        esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(rsp_key));
        esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &passkey, sizeof(passkey));

        // Forzar inicio de seguridad
        esp_ble_gap_security_req(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);
        break;
    }
    case ESP_HIDD_EVENT_BLE_DISCONNECT: {
        ESP_LOGI(TAG, "HID disconnected");
        s_is_connected = false;

        // Reanudar advertising
        esp_ble_adv_params_t adv_params = {
            .adv_int_min = 0x20,
            .adv_int_max = 0x40,
            .adv_type = ADV_TYPE_IND,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
            .channel_map = ADV_CHNL_ALL,
            .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        };
        esp_ble_gap_start_advertising(&adv_params);
        break;
    }
    default:
        break;
    }
}
