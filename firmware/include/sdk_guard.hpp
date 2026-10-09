#pragma once
#include "sdkconfig.h"
#if !CONFIG_SPIRAM_XIP_FROM_PSRAM
#error "Se requiere ESP-IDF con XIP desde PSRAM; no usar SDK Arduino precompilado"
#endif
#if CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE != 64
#error "El modo RGB bounce requiere la configuración de caché de 64 bytes"
#endif
#if !CONFIG_LCD_RGB_ISR_IRAM_SAFE
#error "El ISR RGB debe estar en IRAM"
#endif
