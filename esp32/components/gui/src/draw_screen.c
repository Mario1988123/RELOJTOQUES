#include "draw_screen.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui_fonts.h"
#include "ble_hid_combined.h"  // 🎯 API del HID Combinado
#include <math.h>

static const char *TAG = "DRAW_SCREEN";

static lv_obj_t *s_draw_screen = NULL;
static lv_obj_t *s_draw_area  = NULL;
static lv_obj_t *s_btn_clear  = NULL;
static lv_obj_t *s_lbl_status = NULL;  // Label para mostrar estado de conexión

/* Variables para dibujo continuo */
static lv_point_t s_last_point;
static bool s_has_last_point = false;

/* Variables para control del mouse */
static bool s_mouse_mode = true;  // true = modo mouse, false = modo dibujo
static lv_timer_t *s_status_timer = NULL;

/* ============================================================
 * TIMER PARA ACTUALIZAR ESTADO DE CONEXIÓN
 * ========================================================== */
static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_lbl_status) return;

    bool connected = ble_hid_combined_is_connected();
    if (connected) {
        lv_label_set_text(s_lbl_status, s_mouse_mode ? "MOUSE [Connected]" : "DRAW [Connected]");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0x00FF00), 0);
    } else {
        lv_label_set_text(s_lbl_status, s_mouse_mode ? "MOUSE [Not Connected]" : "DRAW [Not Connected]");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0xFF6060), 0);
    }
}

/* ============================================================
 * EVENTO DEL ÁREA DE DIBUJO MEJORADO (MOUSE + LÍNEAS)
 * ========================================================== */
static void draw_area_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();

    if (!indev) return;

    lv_point_t current_point;
    lv_indev_get_point(indev, &current_point);

    if (code == LV_EVENT_PRESSED) {
        s_last_point = current_point;
        s_has_last_point = true;

        ESP_LOGI(TAG, ">>> PRESSED: modo=%s, pos=(%d,%d), conectado=%d",
                 s_mouse_mode ? "MOUSE" : "DRAW",
                 current_point.x, current_point.y,
                 ble_hid_combined_is_connected());

        // Si está en modo dibujo, crear punto visual
        if (!s_mouse_mode) {
            lv_obj_t *dot = lv_obj_create(s_draw_area);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 8, 8);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_pos(dot, current_point.x - 4, current_point.y - 4);
        }
    }
    else if (code == LV_EVENT_PRESSING && s_has_last_point) {
        lv_coord_t dx = current_point.x - s_last_point.x;
        lv_coord_t dy = current_point.y - s_last_point.y;

        if (s_mouse_mode) {
            // 🎯 MODO MOUSE: Enviar movimiento al HID
            if (dx != 0 || dy != 0) {
                if (!ble_hid_combined_is_connected()) {
                    ESP_LOGW(TAG, "⚠ Mouse NO conectado - movimiento ignorado");
                } else {
                    // Escalar movimiento (dividir por 2 para hacerlo más suave)
                    int8_t mouse_dx = (int8_t)(dx / 2);
                    int8_t mouse_dy = (int8_t)(dy / 2);

                    // Limitar valores a rango -127 a 127
                    if (mouse_dx > 127) mouse_dx = 127;
                    if (mouse_dx < -127) mouse_dx = -127;
                    if (mouse_dy > 127) mouse_dy = 127;
                    if (mouse_dy < -127) mouse_dy = -127;

                    ESP_LOGI(TAG, ">>> ENVIANDO Mouse: dx=%d, dy=%d (raw: %d, %d)",
                             mouse_dx, mouse_dy, dx, dy);
                    ble_hid_mouse_move(mouse_dx, mouse_dy, 0);
                }
            }
        } else {
            // MODO DIBUJO: Dibujar línea visual
            lv_coord_t distance_sq = dx * dx + dy * dy;

            if (distance_sq > 25) {
                lv_coord_t distance = (lv_coord_t)sqrt(distance_sq);
                lv_coord_t steps = distance / 3;

                for (lv_coord_t i = 1; i <= steps; i++) {
                    float t = (float)i / (float)steps;
                    lv_coord_t x = s_last_point.x + (lv_coord_t)(dx * t);
                    lv_coord_t y = s_last_point.y + (lv_coord_t)(dy * t);

                    lv_obj_t *dot = lv_obj_create(s_draw_area);
                    lv_obj_remove_style_all(dot);
                    lv_obj_set_size(dot, 6, 6);
                    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
                    lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
                    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_set_pos(dot, x - 3, y - 3);
                }
            }
        }

        s_last_point = current_point;
    }
    else if (code == LV_EVENT_RELEASED) {
        s_has_last_point = false;
    }
}

/* ============================================================
 * BOTÓN CLEAR MEJORADO
 * ========================================================== */
static void btn_clear_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_draw_area) {
        lv_obj_clean(s_draw_area);
    }

    ESP_LOGI(TAG, "Pantalla de dibujo limpiada");
}

/* ============================================================
 * BOTÓN PARA CAMBIAR MODO (MOUSE ↔ DRAW)
 * ========================================================== */
static void btn_mode_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    s_mouse_mode = !s_mouse_mode;
    ESP_LOGI(TAG, "Modo cambiado a: %s", s_mouse_mode ? "MOUSE" : "DRAW");

    // Actualizar label de estado inmediatamente
    if (s_status_timer) {
        lv_timer_ready(s_status_timer);
    }
}

/* ============================================================
 * CREAR LA PANTALLA DRAW MEJORADA
 * ========================================================== */
lv_obj_t *draw_screen_get(void)
{
    if (s_draw_screen) {
        return s_draw_screen;
    }

    /* Pantalla vacía negra */
    s_draw_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_draw_screen);
    lv_obj_set_style_bg_color(s_draw_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_draw_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_draw_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Título arriba */
    lv_obj_t *label_title = lv_label_create(s_draw_screen);
    lv_label_set_text(label_title, "DRAW / MOUSE");
    lv_obj_set_style_text_color(label_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_title, &font_bold_26, 0);
    lv_obj_set_align(label_title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(label_title, 4);

    /* Label de estado de conexión */
    s_lbl_status = lv_label_create(s_draw_screen);
    lv_label_set_text(s_lbl_status, "MOUSE [Checking...]");
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_lbl_status, &font_normal_26, 0);
    lv_obj_set_align(s_lbl_status, LV_ALIGN_TOP_MID);
    lv_obj_set_y(s_lbl_status, 32);

    /* Área de dibujo mejorada - con márgenes más conservadores */
    s_draw_area = lv_obj_create(s_draw_screen);
    lv_obj_remove_style_all(s_draw_area);
    lv_obj_clear_flag(s_draw_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_draw_area, LV_OPA_TRANSP, 0);

    /* Dimensiones más conservadoras para evitar cortes */
    lv_coord_t scr_w = lv_disp_get_hor_res(NULL);
    lv_coord_t scr_h = lv_disp_get_ver_res(NULL);

    // Dejar espacio para título, estado y botones
    lv_coord_t draw_h = scr_h - 130;  // más espacio para los labels y botones
    lv_coord_t draw_y = 65;            // empezar más abajo

    if (draw_h < 100) draw_h = scr_h - 130;  // mínimo seguro

    lv_obj_set_size(s_draw_area, scr_w - 20, draw_h);
    lv_obj_align(s_draw_area, LV_ALIGN_TOP_MID, 0, draw_y);

    lv_obj_add_flag(s_draw_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_draw_area, draw_area_event_cb, LV_EVENT_ALL, NULL);

    /* Botón MODE - cambiar entre Mouse y Draw */
    lv_obj_t *btn_mode = lv_btn_create(s_draw_screen);
    lv_obj_set_size(btn_mode, 100, 40);
    lv_obj_align(btn_mode, LV_ALIGN_BOTTOM_LEFT, 15, -15);
    lv_obj_set_style_bg_color(btn_mode, lv_color_hex(0x3B82F6), 0);
    lv_obj_set_style_bg_opa(btn_mode, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn_mode, 8, 0);
    lv_obj_add_event_cb(btn_mode, btn_mode_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_mode = lv_label_create(btn_mode);
    lv_label_set_text(lbl_mode, "Mode");
    lv_obj_set_style_text_color(lbl_mode, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_mode, &font_bold_26, 0);
    lv_obj_center(lbl_mode);

    /* Botón CLEAR */
    s_btn_clear = lv_btn_create(s_draw_screen);
    lv_obj_set_size(s_btn_clear, 100, 40);
    lv_obj_align(s_btn_clear, LV_ALIGN_BOTTOM_RIGHT, -15, -15);
    lv_obj_set_style_bg_color(s_btn_clear, lv_color_hex(0xFF6060), 0);
    lv_obj_set_style_bg_opa(s_btn_clear, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_btn_clear, 8, 0);
    lv_obj_add_event_cb(s_btn_clear, btn_clear_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_clear = lv_label_create(s_btn_clear);
    lv_label_set_text(lbl_clear, "Clear");
    lv_obj_set_style_text_color(lbl_clear, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_clear, &font_bold_26, 0);
    lv_obj_center(lbl_clear);

    /* Crear timer para actualizar estado de conexión */
    if (!s_status_timer) {
        s_status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
        lv_timer_ready(s_status_timer);
    }

    ESP_LOGI(TAG, "Pantalla DRAW/MOUSE creada - Resolución: %dx%d", scr_w, scr_h);

    return s_draw_screen;
}