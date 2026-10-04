#ifndef TEST_TASK_H
#define TEST_TASK_H
#include "FreeRTOS.h"
typedef StaticTask_t *TaskHandle_t;
typedef enum { eSetBits } eNotifyAction;
typedef struct { TaskHandle_t xHandle; const char *pcTaskName; uint32_t ulRunTimeCounter; } TaskStatus_t;
TaskHandle_t xTaskCreateStatic(void (*entry)(void *), const char *name,
    uint32_t words, void *argument, UBaseType_t priority,
    StackType_t *stack, StaticTask_t *control);
TickType_t xTaskGetTickCount(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskNotify(TaskHandle_t task, uint32_t value, eNotifyAction action);
BaseType_t xTaskNotifyWait(uint32_t clear_entry, uint32_t clear_exit,
    uint32_t *value, TickType_t timeout);
void vTaskDelay(TickType_t timeout);
void vTaskDelayUntil(TickType_t *previous, TickType_t period);
void vTaskStartScheduler(void);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
UBaseType_t uxTaskGetSystemState(TaskStatus_t *status, UBaseType_t capacity,
    uint32_t *total);
#endif
