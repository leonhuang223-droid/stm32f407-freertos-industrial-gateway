#include "f407_cli_port.h"
#include "f407_fieldbus_port.h"
#include "f407_network_port.h"

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *handle)
{
    f407_cli_uart_tx_complete(handle);
    f407_fieldbus_uart_tx_complete(handle);
    f407_network_uart_tx_complete(handle);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *handle, uint16_t size)
{
    f407_cli_uart_rx_event(handle, size);
    f407_fieldbus_uart_rx_event(handle, size);
    f407_network_uart_rx_event(handle, size);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle)
{
    f407_cli_uart_error(handle);
    f407_fieldbus_uart_error(handle);
    f407_network_uart_error(handle);
}
