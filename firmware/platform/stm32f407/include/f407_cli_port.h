#ifndef F407_CLI_PORT_H
#define F407_CLI_PORT_H

#include "app_context.h"
#include "stm32f4xx_hal.h"

status_t f407_cli_configure(app_context_t *context);
void f407_cli_uart_tx_complete(UART_HandleTypeDef *handle);
void f407_cli_uart_rx_event(UART_HandleTypeDef *handle, uint16_t size);
void f407_cli_uart_error(UART_HandleTypeDef *handle);

#endif
