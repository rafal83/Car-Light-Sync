#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_FREERTOS_UNICORE
#define LED_TASK_CORE tskNO_AFFINITY
#define GENERAL_TASK_CORE tskNO_AFFINITY
static inline BaseType_t create_task_on_led_core(TaskFunction_t task, const char *name, uint32_t stack_depth, void *params, UBaseType_t priority, TaskHandle_t *handle) {
    return xTaskCreate(task, name, stack_depth, params, priority, handle);
}
static inline BaseType_t create_task_on_general_core(TaskFunction_t task, const char *name, uint32_t stack_depth, void *params, UBaseType_t priority, TaskHandle_t *handle) {
    return xTaskCreate(task, name, stack_depth, params, priority, handle);
}
#else
#define LED_TASK_CORE 1
#define GENERAL_TASK_CORE 0
static inline BaseType_t create_task_on_led_core(TaskFunction_t task, const char *name, uint32_t stack_depth, void *params, UBaseType_t priority, TaskHandle_t *handle) {
    return xTaskCreatePinnedToCore(task, name, stack_depth, params, priority, handle, LED_TASK_CORE);
}
static inline BaseType_t create_task_on_general_core(TaskFunction_t task, const char *name, uint32_t stack_depth, void *params, UBaseType_t priority, TaskHandle_t *handle) {
    return xTaskCreatePinnedToCore(task, name, stack_depth, params, priority, handle, GENERAL_TASK_CORE);
}
#endif

