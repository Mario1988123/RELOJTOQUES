#include "ble_hid_combined.h"
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_hidd.h"
#include "esp_hid_common.h"

static const char *TAG = "BLE_HID_MOUSE";

/* Estado interno */
static bool s_ble_started = false;
static bool s_connected = false;
static bool s_sec_conn = false;  // Flag de conexión segura establecida
static bool s_use_pin = false;
static char s_device_name[32] = {0};
static esp_hidd_dev_t *s_hid_dev = NULL;
static uint16_t s_hid_conn_id = 0;  // ID de conexión HID
static esp_bd_addr_t s_peer_bd_addr = {0};  // Dirección del dispositivo conectado

/* HID Report Descriptor MOUSE con Report ID (máxima compatibilidad) */
static const uint8_t s_combined_report_map[] = {
    // Mouse - CON Report ID 0x01 para compatibilidad universal
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x02,       // Usage (Mouse)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report ID (1) - IMPORTANTE para compatibilidad
    0x09, 0x01,       //   Usage (Pointer)
    0xA1, 0x00,       //   Collection (Physical)
    0x05, 0x09,       //     Usage Page (Buttons)
    0x19, 0x01,       //     Usage Minimum (1)
    0x29, 0x03,       //     Usage Maximum (3)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x95, 0x03,       //     Report Count (3)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x02,       //     Input (Data,Var,Abs)
    0x95, 0x01,       //     Report Count (1)
    0x75, 0x05,       //     Report Size (5)
    0x81, 0x01,       //     Input (Const)
    0x05, 0x01,       //     Usage Page (Generic Desktop)
    0x09, 0x30,       //     Usage (X)
    0x09, 0x31,       //     Usage (Y)
    0x09, 0x38,       //     Usage (Wheel)
    0x15, 0x81,       //     Logical Minimum (-127)
    0x25, 0x7F,       //     Logical Maximum (127)
    0x75, 0x08,       //     Report Size (8)
    0x95, 0x03,       //     Report Count (3)
    0x81, 0x06,       //     Input (Data,Var,Rel)
    0xC0,             //   End Collection
    0xC0              // End Collection
};

static esp_hid_raw_report_map_t s_report_maps[] = {
    {
        .data = s_combined_report_map,
        .len = sizeof(s_combined_report_map),
    }
};

static const esp_hid_device_config_t s_hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05DF,
    .version = 0x0100,
    .device_name = "S3Watch Mouse",
    .manufacturer_name = "Pablo",
    .serial_number = "0001",
    .report_maps = s_report_maps,
    .report_maps_len = 1,
};

/* UUID del servicio HID - CRÍTICO para que el host detecte el HID */
static uint8_t s_hid_service_uuid[16] = {
    /* LSB <------------------------------------> MSB */
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

static esp_ble_adv_data_t s_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x20,
    .max_interval = 0x40,
    .appearance = 0x03C2,  // Mouse appearance
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(s_hid_service_uuid),
    .p_service_uuid = s_hid_service_uuid,
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
static void hid_event_handler(void *handler_arg, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    esp_hidd_event_t ev = (esp_hidd_event_t)event_id;
    esp_hidd_event_data_t *ed = (esp_hidd_event_data_t *)event_data;

    // Log TODOS los eventos HID para diagnóstico
    const char *event_name = "UNKNOWN";
    switch (ev) {
        case ESP_HIDD_START_EVENT: event_name = "START"; break;
        case ESP_HIDD_CONNECT_EVENT: event_name = "CONNECT"; break;
        case ESP_HIDD_DISCONNECT_EVENT: event_name = "DISCONNECT"; break;
        case ESP_HIDD_OUTPUT_EVENT: event_name = "OUTPUT"; break;
        case ESP_HIDD_FEATURE_EVENT: event_name = "FEATURE"; break;
        case ESP_HIDD_STOP_EVENT: event_name = "STOP"; break;
        default: break;
    }
    ESP_LOGI(TAG, "!!! HID EVENT: %s (id=%d) !!!", event_name, (int)ev);

    switch (ev) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, ">>> HID START - iniciando advertising");
        esp_ble_gap_config_adv_data(&s_adv_data);
        break;

    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, ">>> HID CONNECTED SUCCESSFULLY! <<<");
        ESP_LOGI(TAG, "========================================");
        s_connected = true;
        s_hid_conn_id++;
        ESP_LOGI(TAG, ">>> ID de conexion HID: %d", s_hid_conn_id);
        ESP_LOGI(TAG, ">>> El dispositivo ahora puede enviar eventos de mouse/keyboard");

        // Si conexión HID antes que emparejamiento, marcarlo
        if (!s_sec_conn) {
            ESP_LOGW(TAG, ">>> ATENCION: HID conectado ANTES del emparejamiento seguro");
            ESP_LOGW(TAG, ">>> Esto puede causar problemas - se recomienda emparejar primero");
        }
        break;

    case ESP_HIDD_DISCONNECT_EVENT:
        ESP_LOGW(TAG, "========================================");
        ESP_LOGW(TAG, ">>> HID DISCONNECTED <<<");
        ESP_LOGW(TAG, "========================================");
        ESP_LOGW(TAG, ">>> Razon: %d", ed->disconnect.reason);
        s_connected = false;
        s_sec_conn = false;
        s_hid_conn_id = 0;
        memset(s_peer_bd_addr, 0, sizeof(esp_bd_addr_t));

        ESP_LOGI(TAG, ">>> Reiniciando advertising...");
        esp_ble_gap_start_advertising(&s_adv_params);
        break;

    case ESP_HIDD_OUTPUT_EVENT:
        ESP_LOGI(TAG, ">>> HID OUTPUT EVENT");
        break;

    case ESP_HIDD_FEATURE_EVENT:
        ESP_LOGI(TAG, ">>> HID FEATURE EVENT");
        break;

    case ESP_HIDD_STOP_EVENT:
        ESP_LOGI(TAG, ">>> HID STOP EVENT");
        break;

    default:
        ESP_LOGW(TAG, ">>> HID evento no manejado: %s (%d)", event_name, (int)ev);
        break;
    }
}


/* Callback GAP - Aceptar emparejamiento automaticamente */
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    ESP_LOGI(TAG, "!!! GAP EVENT: %d !!!", (int)event);

    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, ">>> ADV configurado, iniciando advertising");
        esp_ble_gap_start_advertising(&s_adv_params);
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, ">>> Advertising BLE OK - Dispositivo: %s", s_device_name);
            ESP_LOGI(TAG, "*** EMPAREJAMIENTO SIN PIN (Just Works) ***");
        }
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        ESP_LOGI(TAG, ">>> Advertising DETENIDO");
        break;

    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(TAG, ">>> Parametros de conexion actualizados");
        ESP_LOGI(TAG, "    Status: %d, Min interval: %d, Max interval: %d, Latency: %d, Timeout: %d",
                 param->update_conn_params.status,
                 param->update_conn_params.min_int,
                 param->update_conn_params.max_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);

        // WORKAROUND para bug conocido de ESP-IDF (Issue #11806):
        // ESP_HIDD_CONNECT_EVENT no se dispara en el primer emparejamiento,
        // solo en reconexiones posteriores.
        //
        // Cuando los parametros de conexion se actualizan DESPUES del emparejamiento,
        // es muy probable que HID ya este listo pero el evento no se haya disparado.
        if (s_hid_dev && s_sec_conn && !s_connected && param->update_conn_params.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "    ========================================");
            ESP_LOGW(TAG, "    APLICANDO WORKAROUND PARA BUG ESP-IDF #11806");
            ESP_LOGW(TAG, "    ========================================");
            ESP_LOGW(TAG, "    >>> Conexion segura OK + Parametros actualizados OK");
            ESP_LOGW(TAG, "    >>> ESP_HIDD_CONNECT_EVENT no se ha disparado (bug conocido)");

            // Esperar un momento para que el sistema termine de configurar HID
            vTaskDelay(pdMS_TO_TICKS(500));

            // Verificar si el SDK dice que esta conectado
            bool sdk_connected = esp_hidd_dev_connected(s_hid_dev);
            ESP_LOGW(TAG, "    >>> Verificacion SDK: esp_hidd_dev_connected() = %s",
                     sdk_connected ? "true" : "false");

            // SOLO aplicar workaround si el SDK confirma la conexion HID
            // No asumir conexion HID basandose solo en emparejamiento exitoso
            if (sdk_connected) {
                // Aplicar workaround: marcar como conectado manualmente
                s_connected = true;
                s_hid_conn_id++;

                ESP_LOGW(TAG, "    ========================================");
                ESP_LOGW(TAG, "    !!! WORKAROUND APLICADO !!!");
                ESP_LOGW(TAG, "    >>> HID marcado como CONECTADO manualmente");
                ESP_LOGW(TAG, "    >>> ID de conexion HID: %d", s_hid_conn_id);
                ESP_LOGW(TAG, "    >>> El dispositivo ahora puede enviar eventos");
                ESP_LOGW(TAG, "    ========================================");
            } else {
                ESP_LOGW(TAG, "    [INFO] SDK todavia no reporta conexion HID");
                ESP_LOGW(TAG, "    >>> Esperando evento ESP_HIDD_CONNECT_EVENT...");
            }
        }
        break;

    case ESP_GAP_BLE_SEC_REQ_EVT:
        ESP_LOGI(TAG, "!!! Solicitud de seguridad recibida - ACEPTANDO AUTOMATICAMENTE !!!");
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;

    case ESP_GAP_BLE_NC_REQ_EVT:
        ESP_LOGI(TAG, "!!! Confirmacion numerica solicitada - ACEPTANDO AUTOMATICAMENTE !!!");
        ESP_LOGI(TAG, "    Codigo: %06" PRIu32, param->ble_security.key_notif.passkey);
        esp_ble_confirm_reply(param->ble_security.ble_req.bd_addr, true);
        break;

    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
        ESP_LOGI(TAG, "Passkey notificacion: %06" PRIu32, param->ble_security.key_notif.passkey);
        break;

    case ESP_GAP_BLE_KEY_EVT:
        ESP_LOGI(TAG, "Key recibida, type: %d", param->ble_security.ble_key.key_type);
        break;

    case ESP_GAP_BLE_PASSKEY_REQ_EVT:
        ESP_LOGI(TAG, "Passkey solicitado");
        break;

    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (param->ble_security.auth_cmpl.success) {
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, ">>> EMPAREJAMIENTO EXITOSO! <<<");
            ESP_LOGI(TAG, "    Direccion: %02X:%02X:%02X:%02X:%02X:%02X",
                     param->ble_security.auth_cmpl.bd_addr[0],
                     param->ble_security.auth_cmpl.bd_addr[1],
                     param->ble_security.auth_cmpl.bd_addr[2],
                     param->ble_security.auth_cmpl.bd_addr[3],
                     param->ble_security.auth_cmpl.bd_addr[4],
                     param->ble_security.auth_cmpl.bd_addr[5]);

            // Guardar direccion del peer
            memcpy(s_peer_bd_addr, param->ble_security.auth_cmpl.bd_addr, sizeof(esp_bd_addr_t));

            // Setear flag de conexion segura cuando autenticacion es exitosa
            s_sec_conn = true;
            ESP_LOGI(TAG, "    [OK] CONEXION SEGURA ESTABLECIDA (s_sec_conn=true)");
            ESP_LOGI(TAG, "    >>> El sistema actualizara parametros de conexion automaticamente...");
            ESP_LOGI(TAG, "    >>> Esperando evento ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT...");

            // FIX: NO llamar manualmente a esp_ble_gap_update_conn_params() aqui
            // El sistema BLE ya lo hace automaticamente despues del emparejamiento
            // Llamarlo manualmente causa el warning "l2cble_start_conn_update, the last connection update command still pending"
            // y puede interferir con el establecimiento de la conexion HID

            ESP_LOGI(TAG, "========================================");
        } else {
            ESP_LOGE(TAG, "!!! EMPAREJAMIENTO FALLIDO !!!");
            ESP_LOGE(TAG, "    Codigo error: 0x%02X", param->ble_security.auth_cmpl.fail_reason);
            s_connected = false;
            s_sec_conn = false;
            s_hid_conn_id = 0;
            esp_ble_gap_start_advertising(&s_adv_params);
        }
        break;

    case ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT:
        ESP_LOGI(TAG, "Bond eliminado, reiniciando advertising");
        esp_ble_gap_start_advertising(&s_adv_params);
        break;

    default:
        break;
    }
}

esp_err_t ble_hid_combined_init(const char *device_name, bool use_pin)
{
    if (!device_name) return ESP_ERR_INVALID_ARG;

    strncpy(s_device_name, device_name, sizeof(s_device_name) - 1);
    s_use_pin = use_pin;

    ESP_LOGI(TAG, "Inicializando BLE HID Mouse...");

    if (!s_ble_started) {
        ESP_LOGI(TAG, "Configurando controlador BT...");

        // NO llamar a mem_release aquí, ya se hizo en main
        // esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
        ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
        ESP_ERROR_CHECK(esp_bluedroid_init());
        ESP_ERROR_CHECK(esp_bluedroid_enable());
        ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));

        s_ble_started = true;
    }

    ESP_ERROR_CHECK(esp_ble_gap_set_device_name(s_device_name));

    // Configurar seguridad - SIEMPRE SIN PIN para empezar
    ESP_LOGI(TAG, "Configurando seguridad (SIN PIN)...");

    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t resp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &resp_key, sizeof(resp_key));

    ESP_LOGI(TAG, "Inicializando dispositivo HID...");
    ESP_ERROR_CHECK(esp_hidd_dev_init(&s_hid_config, ESP_HID_TRANSPORT_BLE,
                                      hid_event_handler, &s_hid_dev));

    ESP_LOGI(TAG, "✓ HID Mouse inicializado: %s (SIN PIN)", s_device_name);

    // FORZAR inicio de advertising (el evento START a veces no se dispara)
    ESP_LOGI(TAG, "Forzando inicio de advertising con UUID servicio HID (0x1812)...");
    ESP_LOGI(TAG, "    UUID length: %d bytes", s_adv_data.service_uuid_len);
    vTaskDelay(pdMS_TO_TICKS(100));  // Pequeño delay
    esp_ble_gap_config_adv_data(&s_adv_data);

    return ESP_OK;
}

bool ble_hid_combined_is_connected(void)
{
    // Confiar en el estado reportado por el callback ESP_HIDD_CONNECT_EVENT
    // NO asumir que emparejamiento = conexión HID
    if (!s_hid_dev) {
        return false;
    }

    // Usar el estado del SDK como fuente de verdad
    bool sdk_connected = esp_hidd_dev_connected(s_hid_dev);

    // Si hay discrepancia, logearlo y SINCRONIZAR con el SDK
    if (sdk_connected != s_connected) {
        ESP_LOGW(TAG, "[ESTADO INCONSISTENTE] SDK dice %s pero s_connected=%s",
                 sdk_connected ? "CONECTADO" : "DESCONECTADO",
                 s_connected ? "TRUE" : "FALSE");

        // Sincronizar nuestro estado interno con el SDK
        // El SDK es la fuente de verdad definitiva
        s_connected = sdk_connected;
        ESP_LOGW(TAG, ">>> Sincronizando s_connected con SDK: %s",
                 s_connected ? "TRUE" : "FALSE");
    }

    // Retornar el estado del SDK (fuente de verdad)
    return sdk_connected;
}

/* Mouse functions */
void ble_hid_mouse_move(int8_t dx, int8_t dy, int8_t wheel)
{
    static int consecutive_failures = 0;

    if (!s_hid_dev) {
        if (consecutive_failures < 3) {
            ESP_LOGE(TAG, "[ERROR] s_hid_dev es NULL - HID no inicializado");
            consecutive_failures++;
        }
        return;
    }

    if (!ble_hid_combined_is_connected()) {
        if (consecutive_failures < 3) {
            ESP_LOGW(TAG, "[WARN] Mouse ignorado: HID no conectado (s_connected=%d, sdk=%d)",
                     s_connected, esp_hidd_dev_connected(s_hid_dev));
            consecutive_failures++;
        }
        return;
    }

    // Formato: [Buttons, X, Y, Wheel]
    uint8_t report[4] = {0x00, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel};

    // Report ID = 0x01 (definido en el descriptor con 0x85, 0x01)
    esp_err_t ret = esp_hidd_dev_input_set(s_hid_dev, 0, 0x01, report, sizeof(report));

    if (ret != ESP_OK) {
        consecutive_failures++;
        if (consecutive_failures <= 5) {
            ESP_LOGE(TAG, "[FAIL] Error enviando mouse (%d,%d,%d): %s",
                     dx, dy, wheel, esp_err_to_name(ret));
        }
    } else {
        // Reset contador de fallos en el primer éxito
        if (consecutive_failures > 0) {
            ESP_LOGI(TAG, "[OK] Mouse report enviado exitosamente");
            consecutive_failures = 0;
        }
    }
}

void ble_hid_mouse_buttons(bool left, bool right, bool middle)
{
    if (!s_hid_dev || !ble_hid_combined_is_connected()) return;

    uint8_t buttons = 0;
    if (left) buttons |= 0x01;
    if (right) buttons |= 0x02;
    if (middle) buttons |= 0x04;

    // Report format: [Buttons, X, Y, Wheel]
    uint8_t report[4] = {buttons, 0x00, 0x00, 0x00};
    // Report ID = 0x01 (Mouse)
    esp_hidd_dev_input_set(s_hid_dev, 0, 0x01, report, sizeof(report));

    ESP_LOGI(TAG, "✓ Mouse buttons: L=%d R=%d M=%d (0x%02X)", left, right, middle, buttons);
}

/* Keyboard functions */
static const uint8_t ascii_to_hid[128] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2A, 0x2B, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2C, 0x1E, 0x34, 0x20, 0x21, 0x22, 0x24, 0x34,
    0x26, 0x27, 0x25, 0x2E, 0x36, 0x2D, 0x37, 0x38,
    0x27, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24,
    0x25, 0x26, 0x33, 0x33, 0x36, 0x2E, 0x37, 0x38,
    0x1F, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
    0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
    0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A,
    0x1B, 0x1C, 0x1D, 0x2F, 0x31, 0x30, 0x23, 0x2D,
    0x35, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
    0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
    0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A,
    0x1B, 0x1C, 0x1D, 0x2F, 0x31, 0x30, 0x35, 0x00
};

static void send_keyboard_report(uint8_t modifier, uint8_t keycode)
{
    if (!s_hid_dev || !ble_hid_combined_is_connected()) return;

    // Report format: [Modifier, Reserved, Key1, Key2, Key3, Key4, Key5, Key6] - SIN Report ID
    uint8_t report[8] = {modifier, 0x00, keycode, 0x00, 0x00, 0x00, 0x00, 0x00};
    esp_hidd_dev_input_set(s_hid_dev, 0, 0x02, report, sizeof(report));
    vTaskDelay(pdMS_TO_TICKS(20));

    // Release
    memset(report, 0, 8);
    esp_hidd_dev_input_set(s_hid_dev, 0, 0x02, report, sizeof(report));
    vTaskDelay(pdMS_TO_TICKS(20));
}

void ble_hid_keyboard_send_key(uint8_t keycode)
{
    send_keyboard_report(0x00, keycode);
}

void ble_hid_keyboard_send_text(const char *text)
{
    if (!text) return;

    for (size_t i = 0; text[i] != '\0'; i++) {
        uint8_t ch = (uint8_t)text[i];
        if (ch >= 128) continue;

        uint8_t modifier = 0x00;
        uint8_t keycode = ascii_to_hid[ch];

        if (ch >= 'A' && ch <= 'Z') {
            modifier = 0x02;  // Shift
        }

        if (keycode != 0x00) {
            send_keyboard_report(modifier, keycode);
        }
    }
}
