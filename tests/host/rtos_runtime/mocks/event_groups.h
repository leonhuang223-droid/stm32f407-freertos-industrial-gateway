#ifndef TEST_EVENT_GROUPS_H
#define TEST_EVENT_GROUPS_H
#include "FreeRTOS.h"
typedef uint32_t EventBits_t;
typedef StaticEventGroup_t *EventGroupHandle_t;
EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *control);
EventBits_t xEventGroupGetBits(EventGroupHandle_t group);
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits,
    BaseType_t clear, BaseType_t all, TickType_t timeout);
#endif
