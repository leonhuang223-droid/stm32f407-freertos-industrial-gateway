#include "mock_rtos.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "semphr.h"
#include <assert.h>
#include <string.h>

TickType_t mock_ticks;
void (*mock_wait_hook)(void);
static uint32_t notification;
static unsigned int critical_nesting;
static StaticTask_t current;

void mock_rtos_reset(void)
{
    assert(critical_nesting == 0u);
    mock_ticks = 0u;
    notification = 0u;
    mock_wait_hook = 0;
}
void mock_enter_critical(void) { critical_nesting++; }
void mock_exit_critical(void) { assert(critical_nesting > 0u); critical_nesting--; }
uint32_t mock_runtime_counter(void) { return mock_ticks * 168000u; }

QueueHandle_t xQueueCreateStatic(UBaseType_t capacity, UBaseType_t item_size,
    uint8_t *storage, StaticQueue_t *control)
{
    memset(control, 0, sizeof(*control));
    control->storage = storage; control->capacity = capacity; control->item_size = item_size;
    return control;
}
QueueSetHandle_t xQueueCreateSetStatic(UBaseType_t capacity, uint8_t *storage,
    StaticQueue_t *control)
{ return xQueueCreateStatic(capacity, sizeof(QueueHandle_t), storage, control); }
BaseType_t xQueueAddToSet(QueueHandle_t queue, QueueSetHandle_t set)
{ queue->set = set; return pdPASS; }
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t timeout)
{
    size_t tail;
    if (queue->count == queue->capacity) { mock_ticks += timeout; return pdFAIL; }
    tail = (queue->head + queue->count) % queue->capacity;
    memcpy(queue->storage + tail * queue->item_size, item, queue->item_size);
    queue->count++;
    if (queue->set != 0) { assert(xQueueSend(queue->set, &queue, 0u) == pdPASS); }
    return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t timeout)
{
    if (queue->count == 0u) { mock_ticks += timeout; return pdFAIL; }
    if (queue->set != 0) { assert(queue->selected > 0u); queue->selected--; }
    memcpy(item, queue->storage + queue->head * queue->item_size, queue->item_size);
    queue->head = (queue->head + 1u) % queue->capacity; queue->count--;
    return pdPASS;
}
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item)
{ assert(queue->capacity == 1u && queue->set == 0); queue->count = 0u; return xQueueSend(queue, item, 0u); }
QueueSetMemberHandle_t xQueueSelectFromSet(QueueSetHandle_t set, TickType_t timeout)
{
    QueueHandle_t member = 0;
    if (xQueueReceive(set, &member, timeout) == pdPASS) { member->selected++; }
    return member;
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) { return (UBaseType_t)queue->count; }
void vQueueAddToRegistry(QueueHandle_t queue, const char *name) { (void)queue; (void)name; }

TaskHandle_t xTaskCreateStatic(void (*entry)(void *), const char *name,
    uint32_t words, void *argument, UBaseType_t priority,
    StackType_t *stack, StaticTask_t *control)
{
    (void)name; (void)words; (void)priority; (void)stack;
    control->entry = entry; control->argument = argument; return control;
}
TickType_t xTaskGetTickCount(void) { return mock_ticks; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return &current; }
BaseType_t xTaskNotify(TaskHandle_t task, uint32_t value, eNotifyAction action)
{ (void)task; (void)action; notification |= value; return pdPASS; }
BaseType_t xTaskNotifyWait(uint32_t clear_entry, uint32_t clear_exit,
    uint32_t *value, TickType_t timeout)
{
    notification &= ~clear_entry;
    if (notification == 0u) {
        mock_ticks += timeout;
        if (timeout != 0u && mock_wait_hook != 0) { mock_wait_hook(); }
    }
    *value = notification;
    notification &= ~clear_exit;
    return *value != 0u ? pdTRUE : pdFALSE;
}
void vTaskDelay(TickType_t timeout) { mock_ticks += timeout; }
void vTaskDelayUntil(TickType_t *previous, TickType_t period)
{ *previous += period; if ((int32_t)(*previous - mock_ticks) > 0) { mock_ticks = *previous; } }
void vTaskStartScheduler(void) { }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { (void)task; return 64u; }
UBaseType_t uxTaskGetSystemState(TaskStatus_t *status, UBaseType_t capacity, uint32_t *total)
{ (void)status; (void)capacity; *total = 0u; return 0u; }

EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *control)
{ control->bits = 0u; return control; }
EventBits_t xEventGroupGetBits(EventGroupHandle_t group) { return group->bits; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits)
{ group->bits |= bits; return group->bits; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits)
{ uint32_t old = group->bits; group->bits &= ~bits; return old; }
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits,
    BaseType_t clear, BaseType_t all, TickType_t timeout)
{
    EventBits_t value = group->bits;
    if ((all != pdFALSE && (value & bits) != bits) ||
        (all == pdFALSE && (value & bits) == 0u)) { mock_ticks += timeout; }
    else if (clear != pdFALSE) { group->bits &= ~bits; }
    return value;
}
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *control)
{ control->available = 1; return control; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout)
{ if (!semaphore->available) { mock_ticks += timeout; return pdFAIL; } semaphore->available = 0; return pdPASS; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{ assert(!semaphore->available); semaphore->available = 1; return pdPASS; }
