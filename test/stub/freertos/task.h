#pragma once
#include "FreeRTOS.h"
inline void vTaskDelay(uint32_t) {}
// The harness drives renderBlock() by hand; never start a real thread.
inline int xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*,
                                   uint32_t, TaskHandle_t*, int) { return pdPASS; }
