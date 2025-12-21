#pragma once

#include <stdint.h>
#include <stdbool.h>

/*
 * STUB de BSP Power para S3Watch
 * NO lee el PMIC real, solo devuelve valores fijos
 * para que el proyecto compile y funcione la demo.
 */

// En IDF, esp_event_base_t es básicamente "const char *",
// así que usamos una cadena literal como base de eventos.
#define BSP_POWER_EVENT_BASE "BSP_POWER"

typedef struct {
    bool  vbus_in;
    bool  charging;
    int   battery_percent;
    int   vbus_mv;
    int   batt_mv;
    int   system_mv;
    float temperature_c;
} bsp_power_event_payload_t;

static inline int bsp_power_get_battery_percent(void)
{
    return 100;
}

static inline int bsp_power_get_batt_voltage_mv(void)
{
    return 4000;
}

static inline int bsp_power_get_vbus_voltage_mv(void)
{
    return 0;
}

static inline int bsp_power_get_system_voltage_mv(void)
{
    return 3700;
}

static inline float bsp_power_get_temperature_c(void)
{
    return 30.0f;
}

static inline bool bsp_power_is_charging(void)
{
    return false;
}

static inline bool bsp_power_is_vbus_in(void)
{
    return false;
}

static inline bool bsp_power_poll_pwr_button_short(void)
{
    // Botón de power "nunca pulsado" en este stub
    return false;
}
