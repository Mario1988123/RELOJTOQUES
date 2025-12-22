#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Obtiene la pantalla secreta de selección de carta
 * @return Puntero a la pantalla LVGL
 */
lv_obj_t *secret_card_screen_get(void);

/**
 * @brief Resetea el estado de selección de carta
 */
void secret_card_screen_reset(void);

#ifdef __cplusplus
}
#endif
