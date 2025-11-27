#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Khai báo extern đúng theo tên biến trong .c
// Nếu trong bell_128.c là: const lv_image_dsc_t bell_128 = { ... };
// thì giữ nguyên như bên dưới:
extern const lv_image_dsc_t bell;
extern const lv_image_dsc_t microchip_ai;

#ifdef __cplusplus
}
#endif
