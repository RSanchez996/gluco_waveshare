#pragma once
#include "FreeRTOS.h"
inline int xSemaphoreTake(SemaphoreHandle_t,int){return 1;}
inline void xSemaphoreGive(SemaphoreHandle_t){}
