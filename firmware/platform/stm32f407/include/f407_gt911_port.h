#ifndef GATEWAY_F407_GT911_PORT_H
#define GATEWAY_F407_GT911_PORT_H

#include "gt911.h"

status_t f407_gt911_port_construct(gt911_t *device);
void f407_gt911_port_bind_current_task(void);
uint8_t f407_gt911_port_take_interrupt(void);
/* Cortex-M vector-table entry implemented by the touch port. */
void EXTI9_5_IRQHandler(void);

#endif
