#include "fonts.hpp"
#include <Arduino.h>
#include <initializer_list>

#if !LV_USE_FONT_COMPRESSED
#error "Las fuentes gluco_font_*.c están comprimidas: activar LV_USE_FONT_COMPRESSED"
#endif

extern "C" {
LV_FONT_DECLARE(gluco_font_14)
LV_FONT_DECLARE(gluco_font_16)
LV_FONT_DECLARE(gluco_font_20)
LV_FONT_DECLARE(gluco_font_24)
LV_FONT_DECLARE(gluco_font_28)
}

namespace fonts {
const lv_font_t &montserrat14 = gluco_font_14;
const lv_font_t &montserrat16 = gluco_font_16;
const lv_font_t &montserrat20 = gluco_font_20;
const lv_font_t &montserrat24 = gluco_font_24;
const lv_font_t &montserrat28 = gluco_font_28;
const lv_font_t &montserrat32 = lv_font_montserrat_32;
const lv_font_t &montserrat48 = lv_font_montserrat_48;

void init() {
    for (const lv_font_t *font :
         {&montserrat14, &montserrat16, &montserrat20, &montserrat24, &montserrat28}) {
        if (!font->get_glyph_bitmap(font, 'A') || !font->get_glyph_bitmap(font, 0xF1) || // ñ
            !font->get_glyph_bitmap(font, 0xE1)) {                                       // á
            Serial.println("[FATAL FUENTES] LVGL no puede dibujar caracteres ASCII o españoles");
            Serial.flush();
            while (true)
                delay(1000);
        }
    }
    Serial.println("[FUENTES] Glifos A, ñ y á comprobados en los cinco tamaños");
}
} // namespace fonts
