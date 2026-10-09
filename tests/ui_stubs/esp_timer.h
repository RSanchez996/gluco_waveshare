#pragma once
#include "Arduino.h"
#include <stdint.h>
static inline int64_t esp_timer_get_time(void) { return (int64_t)millis() * 1000; }
