#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Obtiene la pantalla del truco de magia
 * @return Puntero a la pantalla LVGL
 */
lv_obj_t *magic_trick_screen_get(void);

#ifdef __cplusplus
}
#endif
