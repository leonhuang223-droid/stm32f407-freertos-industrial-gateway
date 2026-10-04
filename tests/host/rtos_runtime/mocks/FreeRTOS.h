#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
typedef struct mock_queue {
    uint8_t *storage;
    size_t capacity, item_size, head, count, selected;
    struct mock_queue *set;
} StaticQueue_t;
typedef struct { uint32_t bits; } StaticEventGroup_t;
typedef struct { int available; } StaticSemaphore_t;
typedef struct { void (*entry)(void *); void *argument; } StaticTask_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portTICK_PERIOD_MS 1u
#define portMAX_DELAY UINT32_MAX
void mock_enter_critical(void);
void mock_exit_critical(void);
uint32_t mock_runtime_counter(void);
#define taskENTER_CRITICAL() mock_enter_critical()
#define taskEXIT_CRITICAL() mock_exit_critical()
#define portGET_RUN_TIME_COUNTER_VALUE() mock_runtime_counter()
#endif
