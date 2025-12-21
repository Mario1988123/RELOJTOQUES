#include "secret_card_screen.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui_fonts.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "SECRET_CARD";

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_timeout_timer = NULL;

// Estado de selección
typedef enum {
    STATE_SELECT_SUIT,    // Esperando toques para seleccionar palo (1-4)
    STATE_SELECT_NUMBER,  // Esperando toques para seleccionar número (1-13)
    STATE_TRANSMITTING,   // Transmitiendo por WiFi
    STATE_DONE            // Terminado
} selection_state_t;

static selection_state_t s_state = STATE_SELECT_SUIT;
static int s_tap_count = 0;
static int s_selected_suit = 0;
static int s_selected_number = 0;

// WiFi AP configurado
static bool s_wifi_initialized = false;

/* ============================================================
 * FUNCIONES DE WiFi
 * ========================================================== */

/**
 * @brief Genera SSID con caracteres invisibles para codificar la carta
 * @param suit Palo (1-4)
 * @param number Número (1-13)
 * @param ssid Buffer de salida (mínimo 32 bytes)
 */
static void generate_ssid_with_card(int suit, int number, char *ssid)
{
    // SSID base: "MOE_W"
    strcpy(ssid, "MOE_W");

    // Añadir caracteres invisibles para codificar la carta
    // Usamos caracteres Unicode de ancho cero:
    // U+200B = ZERO WIDTH SPACE (para palo 1 = corazones)
    // U+200C = ZERO WIDTH NON-JOINER (para palo 2 = picas)
    // U+200D = ZERO WIDTH JOINER (para palo 3 = tréboles)
    // U+200E = LEFT-TO-RIGHT MARK (para palo 4 = diamantes)

    const char *suit_chars[] = {
        "",
        "\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B",  // U+200B x3 (corazones)
        "\xE2\x80\x8C\xE2\x80\x8C\xE2\x80\x8C",  // U+200C x3 (picas)
        "\xE2\x80\x8D\xE2\x80\x8D\xE2\x80\x8D",  // U+200D x3 (tréboles)
        "\xE2\x80\x8E\xE2\x80\x8E\xE2\x80\x8E"   // U+200E x3 (diamantes)
    };

    // Añadir código de palo
    if (suit >= 1 && suit <= 4) {
        strcat(ssid, suit_chars[suit]);
    }

    // Codificar número en binario usando caracteres invisibles
    // U+200B = 0, U+200C = 1
    for (int i = 3; i >= 0; i--) {
        if (number & (1 << i)) {
            strcat(ssid, "\xE2\x80\x8C");  // U+200C = 1
        } else {
            strcat(ssid, "\xE2\x80\x8B");  // U+200B = 0
        }
    }
}

/**
 * @brief Transmite beacons WiFi con la carta seleccionada
 */
static void transmit_card_via_wifi(int suit, int number)
{
    ESP_LOGI(TAG, "Transmitiendo carta: Palo=%d, Número=%d", suit, number);

    // Inicializar WiFi si no está inicializado
    if (!s_wifi_initialized) {
        esp_netif_init();
        ESP_ERROR_CHECK(esp_event_loop_create_default());

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

        s_wifi_initialized = true;
    }

    // Generar SSID con la carta codificada
    char ssid[32];
    generate_ssid_with_card(suit, number, ssid);

    ESP_LOGI(TAG, "SSID generado: %s (longitud: %d)", ssid, strlen(ssid));

    // Configurar AP WiFi sin contraseña
    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(ssid),
            .channel = 1,
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = 4,
            .beacon_interval = 100  // Beacon cada 100ms para transmisión rápida
        },
    };

    memcpy(wifi_config.ap.ssid, ssid, strlen(ssid));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP iniciado: SSID='MOE_W' (con datos invisibles)");

    // Mantener WiFi activo durante 5 segundos (50-100 beacons a 100ms)
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Detener WiFi
    esp_wifi_stop();
    ESP_LOGI(TAG, "Transmisión completa");
}

/* ============================================================
 * TIMER DE TIMEOUT (para cambiar de estado)
 * ========================================================== */
static void timeout_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_state == STATE_SELECT_SUIT && s_tap_count > 0) {
        // Confirmar selección de palo y pasar a seleccionar número
        s_selected_suit = s_tap_count;
        if (s_selected_suit > 4) s_selected_suit = 4;

        ESP_LOGI(TAG, "Palo seleccionado: %d (toques: %d)", s_selected_suit, s_tap_count);

        s_state = STATE_SELECT_NUMBER;
        s_tap_count = 0;
    }
    else if (s_state == STATE_SELECT_NUMBER && s_tap_count > 0) {
        // Confirmar selección de número y transmitir
        s_selected_number = s_tap_count;
        if (s_selected_number > 13) s_selected_number = 13;

        ESP_LOGI(TAG, "Número seleccionado: %d (toques: %d)", s_selected_number, s_tap_count);

        s_state = STATE_TRANSMITTING;

        // Transmitir por WiFi
        transmit_card_via_wifi(s_selected_suit, s_selected_number);

        s_state = STATE_DONE;
        ESP_LOGI(TAG, "Selección completa. Volviendo a pantalla principal...");

        // Volver a la pantalla principal después de transmitir
        extern lv_obj_t *watchface_screen_get(void);
        extern void load_screen(lv_obj_t *old_screen, lv_obj_t *new_screen, lv_scr_load_anim_t anim);
        load_screen(s_screen, watchface_screen_get(), LV_SCR_LOAD_ANIM_FADE_IN);
    }
}

/* ============================================================
 * EVENTOS DE TOQUE
 * ========================================================== */
static void screen_events(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        // Incrementar contador de toques
        s_tap_count++;

        ESP_LOGI(TAG, "Toque detectado. Estado=%d, Contador=%d", s_state, s_tap_count);

        // Reiniciar timer de timeout
        if (s_timeout_timer) {
            lv_timer_reset(s_timeout_timer);
        }
    }
    else if (code == LV_EVENT_LONG_PRESSED) {
        // Pulsación larga: volver a pantalla principal
        ESP_LOGI(TAG, "Pulsación larga detectada. Cancelando...");

        extern lv_obj_t *watchface_screen_get(void);
        extern void load_screen(lv_obj_t *old_screen, lv_obj_t *new_screen, lv_scr_load_anim_t anim);
        load_screen(s_screen, watchface_screen_get(), LV_SCR_LOAD_ANIM_FADE_IN);
    }
}

/* ============================================================
 * CREAR PANTALLA
 * ========================================================== */
lv_obj_t *secret_card_screen_get(void)
{
    if (s_screen) {
        // Resetear estado al entrar
        secret_card_screen_reset();
        return s_screen;
    }

    /* Pantalla completamente negra */
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Notificación falsa para disimular */
    lv_obj_t *lbl_notification = lv_label_create(s_screen);
    lv_label_set_text(lbl_notification, "Notificación");
    lv_obj_set_style_text_color(lbl_notification, lv_color_hex(0x404040), 0);
    lv_obj_set_style_text_font(lbl_notification, &font_normal_26, 0);
    lv_obj_set_align(lbl_notification, LV_ALIGN_TOP_MID);
    lv_obj_set_y(lbl_notification, 10);

    /* Crear timer de timeout (2 segundos sin toques = confirmar) */
    if (!s_timeout_timer) {
        s_timeout_timer = lv_timer_create(timeout_timer_cb, 2000, NULL);
        lv_timer_pause(s_timeout_timer);
    }

    /* Eventos de toque en toda la pantalla */
    lv_obj_add_event_cb(s_screen, screen_events, LV_EVENT_ALL, NULL);

    secret_card_screen_reset();

    ESP_LOGI(TAG, "Pantalla secreta creada");
    return s_screen;
}

void secret_card_screen_reset(void)
{
    s_state = STATE_SELECT_SUIT;
    s_tap_count = 0;
    s_selected_suit = 0;
    s_selected_number = 0;

    if (s_timeout_timer) {
        lv_timer_reset(s_timeout_timer);
        lv_timer_resume(s_timeout_timer);
    }

    ESP_LOGI(TAG, "Estado reseteado. Esperando selección de palo...");
}
