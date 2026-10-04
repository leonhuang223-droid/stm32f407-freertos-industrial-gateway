#ifndef TEST_MOCK_RTOS_H
#define TEST_MOCK_RTOS_H
#include "FreeRTOS.h"
extern TickType_t mock_ticks;
extern void (*mock_wait_hook)(void);
void mock_rtos_reset(void);
#endif
