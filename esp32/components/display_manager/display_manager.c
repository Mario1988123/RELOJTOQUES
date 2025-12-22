#include "esp_err.h"                 // <-- PRIMERO: define esp_err_t
#include "esp_check.h"

#include "display_manager.h"
#include "bsp/display.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
//#include "nimble-nordic-uart.h"
#include "settings.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Power management
#include "esp_sleep.h"
#include "sdkconfig.h"
#if CONFIG_PM_ENABLE
#include "esp_pm.h"
#include "bsp_power.h"

// Forward declarations por si el BSP no las declara en un header visible
esp_err_t bsp_display_sleep(void);
esp_err_t bsp_display_wake(void);
esp_err_t bsp_display_clear_black(void);
#endif

// If the board provides simple GPIO buttons, use one as wake key.
// On this hardware BSP_CAPS_BUTTONS is 0, so we will use the PMU PWR key
// instead.
#define DISPLAY_BUTTON GPIO_NUM_0

static const char *TAG = "DISPLAY_MGR";

static bool display_on = true;
static uint32_t timeout_ms;
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t s_no_ls_lock = NULL;
#endif

// -----------------------------------------------------------------------------
// Apagar pantalla internamente (solo panel + brillo, NO LVGL, NO touch)
// -----------------------------------------------------------------------------
static void display_turn_off_internal(void) {
  if (!display_on) {
    return;
  }
  ESP_LOGI(TAG, "Turning display off");

  // Dejamos LVGL y el touch funcionando.
  // Sólo apagamos el panel y la retroiluminación para que parezca apagado,
  // pero el sistema sigue procesando eventos táctiles y BLE sigue vivo.

  // Poner panel en "sleep" (stub o real, según BSP)
  bsp_display_sleep();

  // Apagar retroiluminación
  bsp_display_brightness_set(0);

  display_on = false;
}

void display_manager_turn_off(void) {
  display_turn_off_internal();
}

void display_manager_turn_on(void) {
  if (!display_on) {
    ESP_LOGI(TAG, "Turning display on");
    // Wake the panel first, clear panel, then resume LVGL and restore brightness
    bsp_display_wake();
    (void)bsp_display_clear_black();
    lvgl_port_resume();

    if (lvgl_port_lock(200)) {
#if LVGL_VERSION_MAJOR >= 9
      lv_display_t *disp = lv_display_get_default();
      if (disp) {
        lv_obj_t *scr = lv_scr_act();
        if (scr) {
          lv_obj_invalidate(scr);
        }
      }
#else
      lv_disp_t *disp = lv_disp_get_default();
      if (disp) {
        lv_obj_t *scr = lv_disp_get_scr_act(disp);
        if (scr) {
          lv_obj_invalidate(scr);
        }
      }
#endif
      lvgl_port_unlock();
    }

    bsp_display_brightness_set(settings_get_brightness());
    // Re-enable touch input and release touch reset
    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev) {
      lv_indev_enable(indev, true);
    }
#if defined(BSP_LCD_TOUCH_RST)
    gpio_set_direction(BSP_LCD_TOUCH_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_LCD_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
#endif
    display_on = true;
  }
  // Prevent light sleep while actively displaying UI for responsiveness
#if CONFIG_PM_ENABLE
  if (s_no_ls_lock) {
    (void)esp_pm_lock_acquire(s_no_ls_lock);
  }
#endif
  // Restore more responsive BLE params when screen is on
  // nordic_uart_set_low_power_mode(false);
  display_manager_reset_timer();
}

bool display_manager_is_on(void) {
  return display_on;
}

void display_manager_reset_timer(void) {
  lv_disp_trig_activity(NULL);
}

// -----------------------------------------------------------------------------
// Callback de eventos táctiles de LVGL
// -----------------------------------------------------------------------------
static void touch_event_cb(lv_event_t *e) {
  // Si la pantalla está apagada, cualquier toque la enciende
  if (!display_manager_is_on()) {
    display_manager_turn_on();
    return;
  }

  lv_event_code_t code = lv_event_get_code(e);
  switch (code) {
  case LV_EVENT_PRESSED:
  case LV_EVENT_PRESSING:
  case LV_EVENT_RELEASED:
  case LV_EVENT_CLICKED:
  case LV_EVENT_LONG_PRESSED:
  case LV_EVENT_LONG_PRESSED_REPEAT:
  case LV_EVENT_GESTURE:
    display_manager_reset_timer();
    break;
  default:
    break; // ignore non-input/render events
  }
}

static bool wake_button_pressed(void) {
#if BSP_CAPS_BUTTONS
  return gpio_get_level(DISPLAY_BUTTON) == 0;
#else
  // Poll AXP2101 power key short-press event
  return bsp_power_poll_pwr_button_short();
#endif
}

// -----------------------------------------------------------------------------
// Tarea del display manager — AUTO-OFF DESACTIVADO
// -----------------------------------------------------------------------------
static void display_manager_task(void *arg) {
  ESP_LOGI(TAG, "Display manager task started (auto-off disabled)");
  TickType_t last = xTaskGetTickCount();
  while (1) {
    // Seguimos leyendo el timeout por si hace falta en el futuro,
    // pero NO lo usamos para apagar automáticamente.
    timeout_ms = settings_get_display_timeout();

    if (display_on) {
      // ❌ DESACTIVADO: no apagamos por inactividad
      // uint32_t inactive = lv_disp_get_inactive_time(NULL);
      // if (inactive >= timeout_ms) {
      //   display_turn_off_internal();
      // }

      // El botón PWR puede seguir usándose para "resetear" actividad si quieres
      if (wake_button_pressed()) {
        display_manager_reset_timer();
        vTaskDelay(pdMS_TO_TICKS(100));
        last = xTaskGetTickCount();
      }
    } else {
      // Si por cualquier motivo la pantalla está off, el botón PWR la enciende
      if (wake_button_pressed()) {
        display_manager_turn_on();
        vTaskDelay(pdMS_TO_TICKS(100));
        last = xTaskGetTickCount();
      }
    }
    vTaskDelayUntil(&last, pdMS_TO_TICKS(50));
  }
}

void display_manager_init(void) {
  timeout_ms = settings_get_display_timeout();
#if BSP_CAPS_BUTTONS
  gpio_config_t io_conf = {
      .pin_bit_mask = 1ULL << DISPLAY_BUTTON,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);
#else
  ESP_LOGI(TAG, "Using PMU PWR key to wake display");
#endif

  lv_obj_add_event_cb(
      lv_scr_act(), touch_event_cb,
      LV_EVENT_PRESSED | LV_EVENT_PRESSING | LV_EVENT_RELEASED |
          LV_EVENT_CLICKED | LV_EVENT_LONG_PRESSED |
          LV_EVENT_LONG_PRESSED_REPEAT | LV_EVENT_GESTURE,
      NULL);

  // PM lock may be created in early init; if not, create and acquire now
#if CONFIG_PM_ENABLE
  if (!s_no_ls_lock) {
    (void)esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "display",
                             &s_no_ls_lock);
  }
  if (s_no_ls_lock) {
    (void)esp_pm_lock_acquire(s_no_ls_lock);
  }
#endif

  // Higher priority so UI updates aren't delayed by other workloads
  xTaskCreate(display_manager_task, "display_mgr", 4000, NULL, 3, NULL);
}

void display_manager_pm_early_init(void) {
#if CONFIG_PM_ENABLE
  if (!s_no_ls_lock) {
    (void)esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "display",
                             &s_no_ls_lock);
  }
  if (s_no_ls_lock) {
    (void)esp_pm_lock_acquire(s_no_ls_lock);
  }
#else
  // No power management; nothing to do
  (void)0;
#endif
}

// -----------------------------------------------------------------------------
// Weak stub implementations for optional BSP / RTC helpers
// -----------------------------------------------------------------------------

__attribute__((weak)) esp_err_t bsp_display_sleep(void)
{
    ESP_LOGI(TAG, "bsp_display_sleep() stub – no real low-power panel sleep");
    return ESP_OK;
}

__attribute__((weak)) esp_err_t bsp_display_wake(void)
{
    ESP_LOGI(TAG, "bsp_display_wake() stub – no real panel wake sequence");
    return ESP_OK;
}

__attribute__((weak)) esp_err_t bsp_display_clear_black(void)
{
    ESP_LOGI(TAG, "bsp_display_clear_black() stub – clear LVGL screen to black");
#if CONFIG_BSP_DISPLAY_LVGL_PORT
    if (lvgl_port_lock(0)) {
        lv_obj_t *scr = lv_scr_act();
        if (scr) {
            lv_obj_clean(scr);
            lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
        }
        lvgl_port_unlock();
    }
#endif
    return ESP_OK;
}

__attribute__((weak)) esp_err_t bsp_extra_init(void)
{
    ESP_LOGI(TAG, "bsp_extra_init() stub – nothing to initialise");
    return ESP_OK;
}

__attribute__((weak)) esp_err_t rtc_register_read(uint8_t reg, uint8_t *value)
{
    // Silenciado - genera demasiado spam en los logs
    // ESP_LOGW(TAG, "rtc_register_read() stub – reg=0x%02x, returning 0", reg);
    if (value) {
        *value = 0;
    }
    return ESP_OK;
}

__attribute__((weak)) esp_err_t rtc_register_write(uint8_t reg, uint8_t value)
{
    // Silenciado - genera demasiado spam en los logs
    // ESP_LOGW(TAG, "rtc_register_write() stub – reg=0x%02x, val=0x%02x", reg, value);
    (void)reg;
    (void)value;
    return ESP_OK;
}
