#pragma once
#include <cstdint>
#include <lvgl.h>

namespace weathericons {
struct Icon {
    int code = 0;
    bool day = true;
    uint32_t background = 0x000000;
};

// Formas vectoriales pequeñas: no requieren PNG, sprites ni fuente de emojis.
lv_obj_t *create(lv_obj_t *parent, int x, int y, int size, Icon *data);
} // namespace weathericons
