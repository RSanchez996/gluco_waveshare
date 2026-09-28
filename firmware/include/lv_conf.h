#pragma once
#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC realloc
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#define LV_DPI_DEF 130
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
/* gluco_font_14/16/20/24/28.c are compressed (bitmap_format = 1).
 * Without the decoder LVGL returns NULL for every glyph bitmap. */
#define LV_USE_FONT_COMPRESSED 1
#define LV_USE_QRCODE 1
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_USER_DATA 1
#define LV_USE_THEME_DEFAULT 1
#endif
