#include "ble_mouse_hid.h"

#include <string.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_err.h"

// Núcleo Bluetooth
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"

// HID de Espressif
#include "esp_hidd.h"
#include "esp_hid_common.h"

static const char *TAG = "BLE_HID_MOUSE";

/* -------------------------------------------------------------------------- */
/*  Estado interno                                                            */
/* -------------------------------------------------------------------------- */

static bool   s_ble_started     = false;
static bool   s_connected       = false;
static char   s_device_name[32] = {0};

// Dispositivo HID BLE de Espressif
static esp_hidd_dev_t *s_hid_dev = NULL;

/* -------------------------------------------------------------------------- */
/*  Report descriptor HID de RATÓN (4 bytes: botones, dx, dy, rueda)          */
/* -------------------------------------------------------------------------- */

static const uint8_t s_mouse_report_map[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x02,       // Usage (Mouse)
    0xA1, 0x01,       // Collection (Application)
    0x09, 0x01,       //   Usage (Pointer)
    0xA1, 0x00,       //   Collection (Physical)
    0x05, 0x09,       //     Usage Page (Buttons)
    0x19, 0x01,       //     Usage Minimum (Button 1)
    0x29, 0x03,       //     Usage Maximum (Button 3)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x95, 0x03,       //     Report Count (3)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x02,       //     Input (Data,Var,Abs) – 3 bits botones
    0x95, 0x01,       //     Report Count (1)
    0x75, 0x05,       //     Report Size (5)
    0x81, 0x01,       //     Input (Const,Array,Abs) – padding
    0x05, 0x01,       //     Usage Page (Generic Desktop)
    0x09, 0x30,       //     Usage (X)
    0x09, 0x31,       //     Usage (Y)
    0x09, 0x38,       //     Usage (Wheel)
    0x15, 0x81,       //     Logical Minimum (-127)
    0x25, 0x7F,       //     Logical Maximum (127)
    0x75, 0x08,       //     Report Size (8)
    0x95, 0x03,       //     Report Count (3)
    0x81, 0x06,       //     Input (Data,Var,Rel) – dx, dy, rueda
    0xC0,             //   End Collection
    0xC0              // End Collection
};

static esp_hid_raw_report_map_t s_report_maps[] = {
    {
        .data = s_mouse_report_map,
        .len  = sizeof(s_mouse_report_map),
    }
};

static const esp_hid_device_config_t s_hid_config = {
    .vendor_id         = 0x16C0,          // IDs genéricos (da igual para el truco)
    .product_id        = 0x05DF,
    .version           = 0x0100,
    .device_name       = "S3Watch Mouse", // nombre HID
    .manufacturer_name = "Pablo",
    .serial_number     = "0001",
    .report_maps       = s_report_maps,
    .report_maps_len   = 1,
};

/* -------------------------------------------------------------------------- */
/*  Advertising BLE básico (periférico conectable)                            */
/* -------------------------------------------------------------------------- */

static esp_ble_adv_data_t s_adv_data = {
    .set_scan_rsp        = false,
    .include_name        = true,
    .include_txpower     = true,
    .min_interval        = 0x20,
    .max_interval        = 0x40,
    .appearance          = 0x03C2, // Mouse HID standard
    .manufacturer_len    = 0,
    .p_manufacturer_data = NULL,
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = 0,
    .p_service_uuid      = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min       = 0x20,
    .adv_int_max       = 0x40,
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

/* -------------------------------------------------------------------------- */
/*  Callbacks HID                                                              */
/* -------------------------------------------------------------------------- */

static void hid_event_handler(void *handler_arg,
                              esp_event_base_t base,
                              int32_t event_id,
                              void *event_data)
{
    (void)handler_arg;
    (void)base;
    esp_hidd_event_t      ev  = (esp_hidd_event_t)event_id;
    esp_hidd_event_data_t *ed = (esp_hidd_event_data_t *)event_data;

    switch (ev) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "HID start – empezando advertising BLE");
        esp_ble_gap_config_adv_data(&s_adv_data);
        break;

    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "HID CONNECT: dispositivo conectado");
        s_connected = true;
        break;

    case ESP_HIDD_DISCONNECT_EVENT:
        ESP_LOGW(TAG, "HID DISCONNECT: reason=%d", ed->disconnect.reason);
        s_connected = false;
        // Volver a anunciar para que se pueda reconectar
        esp_ble_gap_start_advertising(&s_adv_params);
        break;

    default:
        break;
    }
}

/* -------------------------------------------------------------------------- */
/*  GAP callback (sólo advertising + pairing simple)                           */
/* -------------------------------------------------------------------------- */

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
            ESP_LOGI(TAG, "Advertising BLE iniciado correctamente");
        } else {
            ESP_LOGE(TAG, "Fallo al iniciar advertising, status=0x%02X",
                     param->adv_start_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (param->ble_security.auth_cmpl.success) {
            ESP_LOGI(TAG,
                     "Emparejamiento completado, success=1, addr=%02X:%02X:%02X:%02X:%02X:%02X",
                     param->ble_security.auth_cmpl.bd_addr[0],
                     param->ble_security.auth_cmpl.bd_addr[1],
                     param->ble_security.auth_cmpl.bd_addr[2],
                     param->ble_security.auth_cmpl.bd_addr[3],
                     param->ble_security.auth_cmpl.bd_addr[4],
                     param->ble_security.auth_cmpl.bd_addr[5]);
            s_connected = true;
        } else {
            ESP_LOGW(TAG, "Emparejamiento FALLÓ, status=0x%02X",
                     param->ble_security.auth_cmpl.fail_reason);
            s_connected = false;
            esp_ble_gap_start_advertising(&s_adv_params);
        }
        break;

    case ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT:
        ESP_LOGI(TAG, "Bond eliminado, volviendo a anunciar");
        s_connected = false;
        esp_ble_gap_start_advertising(&s_adv_params);
        break;

    default:
        break;
    }
}

/* -------------------------------------------------------------------------- */
/*  API pública                                                               */
/* -------------------------------------------------------------------------- */

esp_err_t ble_hid_mouse_init(const char *device_name)
{
    if (device_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Guardar nombre (truncado)
    size_t i = 0;
    while (device_name[i] != '\0' && i < sizeof(s_device_name) - 1) {
        s_device_name[i] = device_name[i];
        i++;
    }
    s_device_name[i] = '\0';

    if (!s_ble_started) {
        ESP_LOGI(TAG, "Inicializando controlador BT solo en modo BLE");

        // Asegura que BT clásico está liberado (no lo usamos)
        esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
        ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

        ESP_ERROR_CHECK(esp_bluedroid_init());
        ESP_ERROR_CHECK(esp_bluedroid_enable());

        // GAP callback
        ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));

        s_ble_started = true;
    }

    // Nombre visible en el escaneo BLE (lo usa también el host)
    ESP_ERROR_CHECK(esp_ble_gap_set_device_name(s_device_name));

    // ------------------------------------------------------------------
    // Seguridad: "Just Works" + bonding (SIN PIN, emparejamiento sencillo)
    // ------------------------------------------------------------------
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_ONLY | ESP_LE_AUTH_BOND;
    uint8_t key_size            = 16;
    uint8_t init_key            = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t resp_key            = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_io_cap_t iocap      = ESP_IO_CAP_NONE;

    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE,
                                   &auth_req, sizeof(auth_req));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE,
                                   &iocap, sizeof(iocap));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE,
                                   &key_size, sizeof(key_size));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY,
                                   &init_key, sizeof(init_key));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY,
                                   &resp_key, sizeof(resp_key));

    // ------------------------------------------------------------------
    // Inicializar dispositivo HID BLE
    // ------------------------------------------------------------------
    ESP_ERROR_CHECK(esp_hidd_dev_init(&s_hid_config,
                                      ESP_HID_TRANSPORT_BLE,
                                      hid_event_handler,
                                      &s_hid_dev));

    // Importante: arrancar ADV (cuando HID START lo pida también lo hará)
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&s_adv_data));

    ESP_LOGI(TAG,
             "BLE periférico inicializado. Nombre \"%s\". "
             "Debería aparecer en el escaneo BLE como ratón.",
             s_device_name);

    s_connected = false;
    return ESP_OK;
}

bool ble_hid_mouse_is_connected(void)
{
    if (!s_hid_dev) {
        return false;
    }
    return esp_hidd_dev_connected(s_hid_dev);
}

/* -------------------------------------------------------------------------- */
/*  Envío de REPORTES HID reales                                             */
/* -------------------------------------------------------------------------- */

static void send_mouse_report(uint8_t buttons,
                              int8_t  dx,
                              int8_t  dy,
                              int8_t  wheel)
{
    if (!s_hid_dev) {
        return;
    }
    if (!esp_hidd_dev_connected(s_hid_dev)) {
        return;
    }

    uint8_t report[4];
    report[0] = buttons;
    report[1] = (uint8_t)dx;
    report[2] = (uint8_t)dy;
    report[3] = (uint8_t)wheel;

    esp_err_t err = esp_hidd_dev_input_set(s_hid_dev,
                                           0,   // índice del report map
                                           0,   // report_id (0: sin Report ID)
                                           report,
                                           sizeof(report));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Fallo al enviar reporte HID: %s", esp_err_to_name(err));
    }
}

void ble_hid_mouse_move(int8_t dx, int8_t dy, int8_t wheel)
{
    // Mantener estado de botones (podríamos guardar último valor si quieres)
    send_mouse_report(0, dx, dy, wheel);
}

void ble_hid_mouse_buttons(bool left, bool right, bool middle)
{
    uint8_t mask = 0;
    if (left)   mask |= 0x01;
    if (right)  mask |= 0x02;
    if (middle) mask |= 0x04;

    send_mouse_report(mask, 0, 0, 0);
}
