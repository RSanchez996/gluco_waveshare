#pragma once
using TaskHandle_t=void *;
using SemaphoreHandle_t=void *;
using QueueHandle_t=void *;
#define portMAX_DELAY 0
#define pdTRUE 1

#define pdMS_TO_TICKS(n) (n)
inline void vTaskDelay(unsigned){}
