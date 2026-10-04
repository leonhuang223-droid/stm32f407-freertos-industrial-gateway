#ifndef TEST_QUEUE_H
#define TEST_QUEUE_H
#include "FreeRTOS.h"
typedef StaticQueue_t *QueueHandle_t;
typedef StaticQueue_t *QueueSetHandle_t;
typedef StaticQueue_t *QueueSetMemberHandle_t;
QueueHandle_t xQueueCreateStatic(UBaseType_t capacity, UBaseType_t item_size,
    uint8_t *storage, StaticQueue_t *control);
QueueSetHandle_t xQueueCreateSetStatic(UBaseType_t capacity, uint8_t *storage,
    StaticQueue_t *control);
BaseType_t xQueueAddToSet(QueueHandle_t queue, QueueSetHandle_t set);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t timeout);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t timeout);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item);
QueueSetMemberHandle_t xQueueSelectFromSet(QueueSetHandle_t set, TickType_t timeout);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
void vQueueAddToRegistry(QueueHandle_t queue, const char *name);
#endif
