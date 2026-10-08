#include "f407_fieldbus_port.h"

#include "can.h"
#include "f407_board_config.h"
#include "main.h"
#include "usart.h"

#include "FreeRTOS.h"
#include "task.h"

#include <limits.h>
#include <string.h>

#define RS485_NOTIFY_TX_DONE (1u << 0)
#define RS485_NOTIFY_RX_DONE (1u << 1)
#define RS485_NOTIFY_ERROR (1u << 2)
#define RS485_NOTIFY_ALL                                                       \
    (RS485_NOTIFY_TX_DONE | RS485_NOTIFY_RX_DONE | RS485_NOTIFY_ERROR)

typedef struct {
    UART_HandleTypeDef *handle;
    TaskHandle_t owner_task;
    volatile size_t rx_length;
} f407_rs485_port_t;

typedef struct {
    CAN_HandleTypeDef *handle;
    TaskHandle_t owner_task;
} f407_can_port_t;

static f407_rs485_port_t rs485_port = {&huart2, 0, 0u};
static f407_can_port_t can_port = {&hcan1, 0};

static status_t status_from_hal(HAL_StatusTypeDef status)
{
    switch (status) {
    case HAL_OK:
        return SYS_OK;
    case HAL_TIMEOUT:
        return ERR_TIMEOUT;
    case HAL_BUSY:
        return ERR_DEVICE_NOT_READY;
    default:
        return ERR_IO;
    }
}

static void clear_current_task_notification(void)
{
    uint32_t ignored;

    (void)xTaskNotifyWait(0u, UINT32_MAX, &ignored, 0u);
}

static status_t wait_for_notification(uint32_t required_bits,
                                      uint32_t timeout_ms,
                                      uint32_t *received_bits)
{
    BaseType_t result;

    result = xTaskNotifyWait(
        0u, UINT32_MAX, received_bits, pdMS_TO_TICKS(timeout_ms));
    if (result != pdTRUE) {
        return ERR_TIMEOUT;
    }
    if ((*received_bits & RS485_NOTIFY_ERROR) != 0u) {
        return ERR_IO;
    }
    return (*received_bits & required_bits) != 0u ? SYS_OK : ERR_PROTOCOL;
}

static status_t f407_rs485_exchange(void *context,
                                    const rs485_transfer_t *parameters,
                                    uint32_t timeout_ms)
{
    if (parameters == 0) {
        return ERR_INVALID_ARG;
    }
    const uint8_t *request = parameters->request;
    size_t request_length = parameters->request_length;
    uint8_t *response = parameters->response;
    size_t response_capacity = parameters->response_capacity;
    size_t *response_length = parameters->response_length;

    f407_rs485_port_t *port = context;
    uint32_t notification_bits = 0u;
    status_t status;

    if (port == 0 || request == 0 || request_length == 0u ||
        request_length > UINT16_MAX || response == 0 ||
        response_capacity == 0u || response_capacity > UINT16_MAX ||
        response_length == 0 || timeout_ms == 0u) {
        return ERR_INVALID_ARG;
    }
    *response_length = 0u;
    vTaskDelay(pdMS_TO_TICKS(F407_MODBUS_FRAME_GAP_MS));
    clear_current_task_notification();
    port->owner_task = xTaskGetCurrentTaskHandle();
    port->rx_length = 0u;

    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);
    status = status_from_hal(HAL_UART_Transmit_DMA(
        port->handle, (uint8_t *)request, (uint16_t)request_length));
    if (status == SYS_OK) {
        status = wait_for_notification(
            RS485_NOTIFY_TX_DONE, timeout_ms, &notification_bits);
    }
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
    if (status != SYS_OK) {
        (void)HAL_UART_AbortTransmit(port->handle);
        port->owner_task = 0;
        return status;
    }

    clear_current_task_notification();
    status = status_from_hal(HAL_UARTEx_ReceiveToIdle_DMA(
        port->handle, response, (uint16_t)response_capacity));
    if (status == SYS_OK && port->handle->hdmarx != 0) {
        __HAL_DMA_DISABLE_IT(port->handle->hdmarx, DMA_IT_HT);
        status = wait_for_notification(
            RS485_NOTIFY_RX_DONE, timeout_ms, &notification_bits);
    }
    if (status != SYS_OK) {
        (void)HAL_UART_AbortReceive(port->handle);
    } else {
        *response_length = port->rx_length;
    }
    port->owner_task = 0;
    return status;
}

static status_t f407_rs485_suspend(void *context)
{
    f407_rs485_port_t *port = context;

    if (port == 0) {
        return ERR_INVALID_ARG;
    }
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
    return status_from_hal(HAL_UART_DeInit(port->handle));
}

static status_t f407_rs485_resume(void *context)
{
    f407_rs485_port_t *port = context;

    if (port == 0) {
        return ERR_INVALID_ARG;
    }
    MX_USART2_UART_Init();
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
    return port->handle->gState == HAL_UART_STATE_READY ? SYS_OK : ERR_IO;
}

static void f407_can_route_to_board_transceiver(void)
{
    GPIO_InitTypeDef gpio = {0};

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static status_t f407_can_filter_all(CAN_HandleTypeDef *handle)
{
    CAN_FilterTypeDef filter = {0};

    filter.FilterBank = 0u;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14u;
    return status_from_hal(HAL_CAN_ConfigFilter(handle, &filter));
}

static status_t f407_can_enable_interrupts(CAN_HandleTypeDef *handle)
{
    return status_from_hal(HAL_CAN_ActivateNotification(
        handle,
        CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_ERROR_WARNING |
            CAN_IT_ERROR_PASSIVE | CAN_IT_BUSOFF | CAN_IT_LAST_ERROR_CODE |
            CAN_IT_ERROR));
}

static status_t f407_can_start(void *context)
{
    f407_can_port_t *port = context;
    status_t status;

    if (port == 0) {
        return ERR_INVALID_ARG;
    }
    status = f407_can_filter_all(port->handle);
    if (status == SYS_OK) {
        status = status_from_hal(HAL_CAN_Start(port->handle));
    }
    if (status == SYS_OK) {
        status = f407_can_enable_interrupts(port->handle);
    }
    return status;
}

static status_t f407_can_send(void *context, const can_frame_t *frame)
{
    f407_can_port_t *port = context;
    CAN_TxHeaderTypeDef header = {0};
    uint32_t mailbox;

    if (port == 0 || frame == 0 || frame->dlc > 8u) {
        return ERR_INVALID_ARG;
    }
    header.IDE = frame->is_extended != 0u ? CAN_ID_EXT : CAN_ID_STD;
    header.RTR = frame->is_remote != 0u ? CAN_RTR_REMOTE : CAN_RTR_DATA;
    header.StdId = frame->id & 0x7ffu;
    header.ExtId = frame->id & 0x1fffffffu;
    header.DLC = frame->dlc;
    return status_from_hal(HAL_CAN_AddTxMessage(
        port->handle, &header, (uint8_t *)frame->data, &mailbox));
}

static status_t f407_can_receive(void *context, can_frame_t *frame)
{
    f407_can_port_t *port = context;
    CAN_RxHeaderTypeDef header = {0};
    HAL_StatusTypeDef hal_status;

    if (port == 0 || frame == 0) {
        return ERR_INVALID_ARG;
    }
    if (HAL_CAN_GetRxFifoFillLevel(port->handle, CAN_RX_FIFO0) == 0u) {
        (void)HAL_CAN_ActivateNotification(port->handle,
                                           CAN_IT_RX_FIFO0_MSG_PENDING);
        return ERR_DEVICE_NOT_READY;
    }
    memset(frame, 0, sizeof(*frame));
    hal_status =
        HAL_CAN_GetRxMessage(port->handle, CAN_RX_FIFO0, &header, frame->data);
    if (hal_status != HAL_OK) {
        return status_from_hal(hal_status);
    }
    frame->is_extended = header.IDE == CAN_ID_EXT ? 1u : 0u;
    frame->is_remote = header.RTR == CAN_RTR_REMOTE ? 1u : 0u;
    frame->id = frame->is_extended != 0u ? header.ExtId : header.StdId;
    frame->dlc = (uint8_t)header.DLC;
    if (HAL_CAN_GetRxFifoFillLevel(port->handle, CAN_RX_FIFO0) == 0u) {
        (void)HAL_CAN_ActivateNotification(port->handle,
                                           CAN_IT_RX_FIFO0_MSG_PENDING);
    }
    return SYS_OK;
}

static status_t
f407_can_wait_event(void *context, uint32_t timeout_ms, uint32_t *event_bits)
{
    f407_can_port_t *port = context;

    if (port == 0 || event_bits == 0) {
        return ERR_INVALID_ARG;
    }
    port->owner_task = xTaskGetCurrentTaskHandle();
    if (HAL_CAN_GetRxFifoFillLevel(port->handle, CAN_RX_FIFO0) != 0u) {
        *event_bits = CAN_BUS_EVENT_RX;
        return SYS_OK;
    }
    clear_current_task_notification();
    (void)HAL_CAN_ActivateNotification(port->handle,
                                       CAN_IT_RX_FIFO0_MSG_PENDING);
    if (HAL_CAN_GetRxFifoFillLevel(port->handle, CAN_RX_FIFO0) != 0u) {
        *event_bits = CAN_BUS_EVENT_RX;
        return SYS_OK;
    }
    if (xTaskNotifyWait(
            0u, UINT32_MAX, event_bits, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        *event_bits = CAN_BUS_EVENT_NONE;
        return ERR_TIMEOUT;
    }
    return SYS_OK;
}

static status_t f407_can_get_state(void *context, can_bus_state_t *state)
{
    f407_can_port_t *port = context;
    uint32_t esr;

    if (port == 0 || state == 0) {
        return ERR_INVALID_ARG;
    }
    esr = port->handle->Instance->ESR;
    if ((esr & CAN_ESR_BOFF) != 0u) {
        *state = CAN_BUS_OFF;
    } else if ((esr & CAN_ESR_EPVF) != 0u) {
        *state = CAN_BUS_ERROR_PASSIVE;
    } else if ((esr & CAN_ESR_EWGF) != 0u) {
        *state = CAN_BUS_ERROR_WARNING;
    } else {
        *state = CAN_BUS_ACTIVE;
    }
    return SYS_OK;
}

static status_t f407_can_recover(void *context)
{
    f407_can_port_t *port = context;
    status_t status;

    if (port == 0) {
        return ERR_INVALID_ARG;
    }
    status = status_from_hal(HAL_CAN_Stop(port->handle));
    if (status == SYS_OK) {
        status = status_from_hal(HAL_CAN_Start(port->handle));
    }
    if (status == SYS_OK) {
        status = f407_can_enable_interrupts(port->handle);
    }
    return status;
}

static status_t f407_can_suspend(void *context)
{
    f407_can_port_t *port = context;

    return port != 0 ? status_from_hal(HAL_CAN_RequestSleep(port->handle))
                     : ERR_INVALID_ARG;
}

static status_t f407_can_resume(void *context)
{
    f407_can_port_t *port = context;

    return port != 0 ? status_from_hal(HAL_CAN_WakeUp(port->handle))
                     : ERR_INVALID_ARG;
}

static void notify_task_from_isr(TaskHandle_t task, uint32_t event_bits)
{
    BaseType_t should_yield = pdFALSE;

    if (task != 0) {
        (void)xTaskNotifyFromISR(task, event_bits, eSetBits, &should_yield);
        portYIELD_FROM_ISR(should_yield);
    }
}

void f407_fieldbus_uart_tx_complete(UART_HandleTypeDef *handle)
{
    if (handle == rs485_port.handle) {
        notify_task_from_isr(rs485_port.owner_task, RS485_NOTIFY_TX_DONE);
    }
}

void f407_fieldbus_uart_rx_event(UART_HandleTypeDef *handle, uint16_t size)
{
    if (handle == rs485_port.handle) {
        rs485_port.rx_length = size;
        notify_task_from_isr(rs485_port.owner_task, RS485_NOTIFY_RX_DONE);
    }
}

void f407_fieldbus_uart_error(UART_HandleTypeDef *handle)
{
    if (handle == rs485_port.handle) {
        notify_task_from_isr(rs485_port.owner_task, RS485_NOTIFY_ERROR);
    }
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *handle)
{
    if (handle == can_port.handle) {
        /* FMP is level-triggered. Leave FIFO ownership to CanTask and mask
         * the source until that task drains it, including before scheduling. */
        (void)HAL_CAN_DeactivateNotification(handle,
                                             CAN_IT_RX_FIFO0_MSG_PENDING);
        notify_task_from_isr(can_port.owner_task, CAN_BUS_EVENT_RX);
    }
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *handle)
{
    if (handle == can_port.handle) {
        notify_task_from_isr(can_port.owner_task, CAN_BUS_EVENT_ERROR);
    }
}

status_t f407_fieldbus_configure(app_context_t *context)
{
    static const rs485_bus_ops_t rs485_ops = {
        f407_rs485_exchange, f407_rs485_suspend, f407_rs485_resume};
    static const can_bus_ops_t can_ops = {f407_can_start,
                                          f407_can_send,
                                          f407_can_receive,
                                          f407_can_wait_event,
                                          f407_can_get_state,
                                          f407_can_recover,
                                          f407_can_suspend,
                                          f407_can_resume};
    static const modbus_poll_entry_t poll_table[] = {
        {F407_MODBUS_SLAVE_ID,
         MODBUS_FUNCTION_READ_HOLDING,
         F407_MODBUS_START_REGISTER,
         1u,
         0u,
         MODBUS_VALUE_S16,
         MODBUS_WORD_HIGH_FIRST,
         1,
         1,
         0,
         GATEWAY_POINT_MODBUS_PROCESS_VALUE,
         GATEWAY_UNIT_RAW}};
    app_fieldbus_config_t config = {0};

    if (context == 0) {
        return ERR_INVALID_ARG;
    }
    MX_USART2_UART_Init();
    MX_CAN1_Init();
    f407_can_route_to_board_transceiver();
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);

    config.rs485_ops = &rs485_ops;
    config.rs485_context = &rs485_port;
    config.modbus_timeout_ms = F407_MODBUS_RESPONSE_TIMEOUT_MS;
    config.modbus_poll_table = poll_table;
    config.modbus_poll_count = sizeof(poll_table) / sizeof(poll_table[0]);
    config.modbus_retry_limit = F407_MODBUS_RETRY_LIMIT;
    config.can_ops = &can_ops;
    config.can_context = &can_port;
    config.local_can_node_id = F407_CAN_LOCAL_NODE_ID;
    config.can_recovery_delay_ms = F407_CAN_RECOVERY_DELAY_MS;
    return app_context_configure_fieldbus(context, &config);
}
