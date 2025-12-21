#ifndef DRAW_SCREEN_H
#define DRAW_SCREEN_H

#include "lvgl.h"

/* Devuelve el objeto raíz de la pantalla de dibujo (se crea lazy) */
lv_obj_t *draw_screen_get(void);

#endif // DRAW_SCREEN_H
