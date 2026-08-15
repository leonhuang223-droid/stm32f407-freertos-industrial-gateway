#ifndef F407_NETWORK_PORT_H
#define F407_NETWORK_PORT_H

#include "app_context.h"
#include "stm32f4xx_hal.h"

status_t f407_network_configure(app_context_t *context);
void f407_network_uart_tx_complete(UART_HandleTypeDef *handle);
void f407_network_uart_rx_event(UART_HandleTypeDef *handle, uint16_t size);
void f407_network_uart_error(UART_HandleTypeDef *handle);

#endif
