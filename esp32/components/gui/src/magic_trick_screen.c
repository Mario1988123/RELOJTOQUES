#include "magic_trick_screen.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui_fonts.h"
#include "ble_hid_combined.h"
#include <stdio.h>

static const char *TAG = "MAGIC_TRICK";

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_lbl_status = NULL;   // Estado de conexión
static lv_obj_t *s_lbl_time = NULL;     // Display central tipo reloj "HH:MM"
static lv_timer_t *s_status_timer = NULL;

static int s_suit = 1;   // 1=Corazones, 2=Picas, 3=Tréboles, 4=Diamantes (mostrado como 01-04)
static int s_value = 1;  // 1=As, 2-10=números, 11=J, 12=Q, 13=K (mostrado como 01-13)

/* Nombres de palos en español */
static const char *suit_names[] = {
    "",
    "CORAZONES",  // 1
    "PICAS",      // 2
    "TREBOLES",   // 3
    "DIAMANTES"   // 4
};

/* Símbolos Unicode de palos */
static const char *suit_symbols[] = {
    "",
    "♥",  // Corazones
    "♠",  // Picas
    "♣",  // Tréboles
    "♦"   // Diamantes
};

/* Nombres de valores */
static const char *value_names[] = {
    "",
    "AS", "2", "3", "4", "5", "6", "7", "8", "9", "10",
    "J", "Q", "K"
};

/* ============================================================
 * TIMER PARA ACTUALIZAR ESTADO
 * ========================================================== */
static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_lbl_status) return;

    bool connected = ble_hid_combined_is_connected();
    if (connected) {
        lv_label_set_text(s_lbl_status, "C");  // Solo una letra, casi invisible
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0x252525), 0);
    } else {
        lv_label_set_text(s_lbl_status, "");  // Vacío si no está conectado
    }
}

/* ============================================================
 * ACTUALIZAR DISPLAY
 * ========================================================== */
static void update_display(void)
{
    // Actualizar display central tipo reloj "HH:MM"
    // HH = palo (01-04), MM = valor (01-13)
    if (s_lbl_time && s_suit >= 1 && s_suit <= 4 && s_value >= 1 && s_value <= 13) {
        char time_text[8];
        snprintf(time_text, sizeof(time_text), "%02d:%02d", s_suit, s_value);
        lv_label_set_text(s_lbl_time, time_text);
    }
}

/* ============================================================
 * ENVIAR CARTA AL MÓVIL
 * ========================================================== */
static void send_card_to_phone(void)
{
    if (!ble_hid_combined_is_connected()) {
        ESP_LOGW(TAG, "No hay conexión HID Keyboard");
        return;
    }

    // Formato: "AS de CORAZONES"
    char card_text[64];
    snprintf(card_text, sizeof(card_text), "%s de %s",
             value_names[s_value], suit_names[s_suit]);

    ESP_LOGI(TAG, "Enviando carta: %s", card_text);
    ble_hid_keyboard_send_text(card_text);
    ble_hid_keyboard_send_key(HID_KEY_ENTER);  // Enviar Enter al final
}

/* ============================================================
 * BOTONES DE CONTROL
 * ========================================================== */
static void btn_hour_up_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    s_suit++;
    if (s_suit > 4) s_suit = 1;
    update_display();
    ESP_LOGI(TAG, "Hora (Palo): %02d - %s", s_suit, suit_names[s_suit]);
}

static void btn_hour_down_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    s_suit--;
    if (s_suit < 1) s_suit = 4;
    update_display();
    ESP_LOGI(TAG, "Hora (Palo): %02d - %s", s_suit, suit_names[s_suit]);
}

static void btn_minute_up_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    s_value++;
    if (s_value > 13) s_value = 1;
    update_display();
    ESP_LOGI(TAG, "Minuto (Valor): %02d - %s", s_value, value_names[s_value]);
}

static void btn_minute_down_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    s_value--;
    if (s_value < 1) s_value = 13;
    update_display();
    ESP_LOGI(TAG, "Minuto (Valor): %02d - %s", s_value, value_names[s_value]);
}

static void btn_send_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    send_card_to_phone();
}

/* ============================================================
 * CREAR PANTALLA
 * ========================================================== */
lv_obj_t *magic_trick_screen_get(void)
{
    if (s_screen) {
        return s_screen;
    }

    /* Pantalla negra */
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Título - MOE en gris oscuro (casi invisible) */
    lv_obj_t *lbl_title = lv_label_create(s_screen);
    lv_label_set_text(lbl_title, "MOE");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x202020), 0);
    lv_obj_set_style_text_font(lbl_title, &font_normal_26, 0);
    lv_obj_set_align(lbl_title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(lbl_title, 5);

    /* Estado de conexión - gris oscuro */
    s_lbl_status = lv_label_create(s_screen);
    lv_label_set_text(s_lbl_status, "");
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0x303030), 0);
    lv_obj_set_style_text_font(s_lbl_status, &font_normal_26, 0);
    lv_obj_set_align(s_lbl_status, LV_ALIGN_TOP_MID);
    lv_obj_set_y(s_lbl_status, 30);

    /* ========================================================
     * DISPLAY CENTRAL TIPO RELOJ "HH:MM"
     * ======================================================== */
    s_lbl_time = lv_label_create(s_screen);
    lv_label_set_text(s_lbl_time, "01:01");
    lv_obj_set_style_text_color(s_lbl_time, lv_color_hex(0xFFFFFF), 0);  // Blanco para que se vea
    lv_obj_set_style_text_font(s_lbl_time, &font_bold_42, 0);
    lv_obj_align(s_lbl_time, LV_ALIGN_CENTER, 0, -20);

    /* ========================================================
     * BOTONES DE CONTROL PARA "HORAS" (PALOS) - IZQUIERDA
     * ======================================================== */
    /* Botón UP horas */
    lv_obj_t *btn_hour_up = lv_btn_create(s_screen);
    lv_obj_set_size(btn_hour_up, 60, 50);
    lv_obj_align(btn_hour_up, LV_ALIGN_CENTER, -120, -60);
    lv_obj_set_style_bg_color(btn_hour_up, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(btn_hour_up, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_hour_up, 8, 0);
    lv_obj_add_event_cb(btn_hour_up, btn_hour_up_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_hour_up = lv_label_create(btn_hour_up);
    lv_label_set_text(lbl_hour_up, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(lbl_hour_up, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(lbl_hour_up, &font_normal_26, 0);
    lv_obj_center(lbl_hour_up);

    /* Botón DOWN horas */
    lv_obj_t *btn_hour_down = lv_btn_create(s_screen);
    lv_obj_set_size(btn_hour_down, 60, 50);
    lv_obj_align(btn_hour_down, LV_ALIGN_CENTER, -120, 20);
    lv_obj_set_style_bg_color(btn_hour_down, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(btn_hour_down, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_hour_down, 8, 0);
    lv_obj_add_event_cb(btn_hour_down, btn_hour_down_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_hour_down = lv_label_create(btn_hour_down);
    lv_label_set_text(lbl_hour_down, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(lbl_hour_down, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(lbl_hour_down, &font_normal_26, 0);
    lv_obj_center(lbl_hour_down);

    /* ========================================================
     * BOTONES DE CONTROL PARA "MINUTOS" (VALORES) - DERECHA
     * ======================================================== */
    /* Botón UP minutos */
    lv_obj_t *btn_minute_up = lv_btn_create(s_screen);
    lv_obj_set_size(btn_minute_up, 60, 50);
    lv_obj_align(btn_minute_up, LV_ALIGN_CENTER, 120, -60);
    lv_obj_set_style_bg_color(btn_minute_up, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(btn_minute_up, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_minute_up, 8, 0);
    lv_obj_add_event_cb(btn_minute_up, btn_minute_up_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_minute_up = lv_label_create(btn_minute_up);
    lv_label_set_text(lbl_minute_up, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(lbl_minute_up, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(lbl_minute_up, &font_normal_26, 0);
    lv_obj_center(lbl_minute_up);

    /* Botón DOWN minutos */
    lv_obj_t *btn_minute_down = lv_btn_create(s_screen);
    lv_obj_set_size(btn_minute_down, 60, 50);
    lv_obj_align(btn_minute_down, LV_ALIGN_CENTER, 120, 20);
    lv_obj_set_style_bg_color(btn_minute_down, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(btn_minute_down, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_minute_down, 8, 0);
    lv_obj_add_event_cb(btn_minute_down, btn_minute_down_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_minute_down = lv_label_create(btn_minute_down);
    lv_label_set_text(lbl_minute_down, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(lbl_minute_down, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(lbl_minute_down, &font_normal_26, 0);
    lv_obj_center(lbl_minute_down);

    /* ========================================================
     * BOTÓN DE ENVÍO - Pequeño botón de color sin texto
     * ======================================================== */
    lv_obj_t *btn_send = lv_btn_create(s_screen);
    lv_obj_set_size(btn_send, 80, 80);
    lv_obj_align(btn_send, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_bg_color(btn_send, lv_color_hex(0x0080FF), 0);  // Azul
    lv_obj_set_style_bg_opa(btn_send, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_send, 40, 0);  // Circular
    lv_obj_add_event_cb(btn_send, btn_send_cb, LV_EVENT_CLICKED, NULL);

    /* Crear timer para estado */
    if (!s_status_timer) {
        s_status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
        lv_timer_ready(s_status_timer);
    }

    update_display();

    ESP_LOGI(TAG, "Pantalla Magic Trick creada (modo reloj)");
    return s_screen;
}
