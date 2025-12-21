#include "ble_mouse.h"

#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_MOUSE";

static uint8_t own_addr_type;

/* Adelantos de funciones internas */
static void ble_app_advertise(void);
static int ble_gap_event(struct ble_gap_event *event, void *arg);
static void ble_on_sync(void *arg);
static void ble_host_task(void *param);

/* Tabla mínima de servicios GATT (solo GAP y GATT genéricos) */
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        /*** Service: GAP (ya lo maneja ble_svc_gap, pero ponemos la entrada) */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_UUID16_GAP_DEVICE_NAME),
        .characteristics = NULL,
    },
    {
        /*** Service: GATT (también genérico) */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_UUID16_GATT_SERVICE_CHANGED),
        .characteristics = NULL,
    },

    { 0 } /* terminador */
};

/* Evento principal de GAP: conexiones, desconexiones, etc. */
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "Conectado, handle=%d", event->connect.conn_handle);
        } else {
            ESP_LOGW(TAG, "Fallo al conectar; status=%d, re-anunciando", event->connect.status);
            ble_app_advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Desconectado, motivo=%d, re-anunciando", event->disconnect.reason);
        ble_app_advertise();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertising completo, re-anunciando");
        ble_app_advertise();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "SUBSCRIBE: val_handle=%d, enabled=%d",
                 event->subscribe.attr_handle, event->subscribe.cur_notify);
        break;

    default:
        break;
    }

    return 0;
}

/* Empieza el advertising como periférico BLE */
static void ble_app_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));

    /* Flags típicos para BLE periférico */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* Nombre del dispositivo desde ble_svc_gap */
    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields fallo; rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  // connectable undirected
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start fallo; rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising iniciado");
    }
}

/* Se llama cuando NimBLE está sincronizado con el controlador */
static void ble_on_sync(void *arg)
{
    int rc;

    /* Obtener tipo de dirección propia */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto fallo; rc=%d", rc);
        return;
    }

    uint8_t addr_val[6] = {0};
    ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    ESP_LOGI(TAG, "Dirección BLE propia: %02X:%02X:%02X:%02X:%02X:%02X",
             addr_val[5], addr_val[4], addr_val[3],
             addr_val[2], addr_val[1], addr_val[0]);

    /* Empezar a anunciar */
    ble_app_advertise();
}

/* Tarea del host NimBLE */
static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task start");
    nimble_port_run();   // Esta función no sale hasta que se llame a nimble_port_stop()
    nimble_port_freertos_deinit();
}

/* Función pública: iniciar el BLE básico */
esp_err_t ble_mouse_start(const char *device_name)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Inicializando NVS");
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Inicializando controlador HCI NimBLE");
    ESP_ERROR_CHECK(esp_nimble_hci_and_controller_init());

    ESP_LOGI(TAG, "Inicializando NimBLE host");
    nimble_port_init();

    /* Inicializar GAP y GATT genéricos */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* Registrar nuestra tabla de servicios (de momento vacía) */
    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg fallo; rc=%d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs fallo; rc=%d", rc);
        return ESP_FAIL;
    }

    /* Ajustar nombre que se anuncia */
    if (device_name == NULL) {
        device_name = "S3Watch Mouse";
    }
    rc = ble_svc_gap_device_name_set(device_name);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_svc_gap_device_name_set fallo; rc=%d", rc);
        return ESP_FAIL;
    }

    /* Config NimBLE callbacks */
    ble_hs_cfg.reset_cb = NULL;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Tarea del host NimBLE en FreeRTOS */
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "ble_mouse_start OK (esperando a sync para anunciar)");

    return ESP_OK;
}
