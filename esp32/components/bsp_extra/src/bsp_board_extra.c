// Stubbed bsp_board_extra.c for Arduino S3Watch port
// This file disables the original ESP-Event based board helpers so we
// don't need esp_event.h in this project.  If at some point you want to
// use those features, we can re‑implement them here.

#include <stdbool.h>
#include <stdint.h>

// Intentionally left mostly empty.  If some symbol from the original
// bsp_board_extra.c is required at link time, we can add a tiny stub
// implementation here matching the prototype from bsp_board_extra.h.
