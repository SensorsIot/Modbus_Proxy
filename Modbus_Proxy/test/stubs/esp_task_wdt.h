#pragma once
#include <cstdint>

typedef void* esp_err_t;
#define ESP_OK nullptr

inline esp_err_t esp_task_wdt_init(uint32_t, bool) { return ESP_OK; }
inline esp_err_t esp_task_wdt_add(void*) { return ESP_OK; }
inline esp_err_t esp_task_wdt_reset() { return ESP_OK; }
