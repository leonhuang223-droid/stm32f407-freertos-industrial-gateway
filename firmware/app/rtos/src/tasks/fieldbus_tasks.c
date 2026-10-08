#include "runtime_internal.h"
#include <stdio.h>
#include <string.h>

void modbus_task(void *argument)
{
    app_fieldbus_context_t *context = argument;
    TickType_t next_wake;
    uint8_t power_suspended = 0u;

    (void)xEventGroupWaitBits(channels.system_events,
                              SYSTEM_EVENT_FIELDBUS_READY,
                              pdFALSE,
                              pdTRUE,
                              portMAX_DELAY);
    next_wake = xTaskGetTickCount();

    for (;;) {
        gateway_measurement_t measurement;
        EventBits_t power_bits = xEventGroupGetBits(channels.system_events);

        if ((power_bits & SYSTEM_EVENT_POWER_QUIESCE_REQUEST) != 0u) {
            if (power_suspended == 0u) {
                status_t status = rs485_bus_suspend(context->modbus_rs485);

                if (status == SYS_OK) {
                    power_suspended = 1u;
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_POWER_ACK_MODBUS);
                } else {
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_FAULT_ACTIVE);
                }
            }
            app_runtime_mark_alive(GATEWAY_TASK_MODBUS);
            vTaskDelay(pdMS_TO_TICKS(20u));
            continue;
        }
        if (power_suspended != 0u) {
            status_t status = rs485_bus_resume(context->modbus_rs485);

            xEventGroupClearBits(channels.system_events,
                                 SYSTEM_EVENT_POWER_ACK_MODBUS);
            power_suspended = status == SYS_OK ? 0u : 1u;
            next_wake = xTaskGetTickCount();
            if (status != SYS_OK) {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
                app_runtime_mark_alive(GATEWAY_TASK_MODBUS);
                vTaskDelay(pdMS_TO_TICKS(20u));
                continue;
            }
        }

        if (power_lock_acquire(PM_LOCK_MODBUS_TRANSACTION) == SYS_OK) {
            (void)fieldbus_subsystem_poll_modbus(
                context->fieldbus,
                (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                &measurement);
            (void)power_lock_release(PM_LOCK_MODBUS_TRANSACTION);
        } else {
            memset(&measurement, 0, sizeof(measurement));
            measurement.source = GATEWAY_SOURCE_MODBUS;
            measurement.quality = GATEWAY_QUALITY_UNAVAILABLE;
            measurement.error = ERR_DEVICE_NOT_READY;
        }
        if (app_rtos_publish_measurement(&measurement) != SYS_OK) {
            (*context->measurement_publish_drops)++;
        }
        app_runtime_mark_alive(GATEWAY_TASK_MODBUS);
        vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(1000U));
    }
}

static void service_can_frames(app_fieldbus_context_t *context)
{
    gateway_measurement_t measurements[4];
    gateway_can_tx_message_t message;
    uint32_t event_bits = CAN_BUS_EVENT_NONE;
    size_t count = 0u;
    size_t i;
    status_t wait_status;

    wait_status =
        fieldbus_subsystem_wait_can(context->fieldbus, 20u, &event_bits);
    if (wait_status == ERR_DEVICE_NOT_READY) {
        vTaskDelay(pdMS_TO_TICKS(20u));
    }
    (void)fieldbus_subsystem_process_can(
        context->fieldbus,
        &(const fieldbus_can_process_t){
            event_bits,
            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
            measurements,
            4u,
            &count});
    for (i = 0u; i < count; ++i) {
        if (app_rtos_publish_measurement(&measurements[i]) != SYS_OK) {
            (*context->measurement_publish_drops)++;
        }
    }
    while (xQueueReceive(channels.can_tx, &message, 0U) == pdPASS) {
        (void)fieldbus_subsystem_send_can(context->fieldbus,
                                          &message.measurement);
    }
}

static int handle_can_power(app_fieldbus_context_t *context,
                            uint8_t *power_suspended,
                            uint8_t *can_lock_held,
                            EventBits_t power_bits)
{
    if ((power_bits & SYSTEM_EVENT_POWER_QUIESCE_REQUEST) != 0u) {
        if ((*power_suspended) == 0u) {
            status_t status = can_bus_suspend(context->can_bus);

            if (status == SYS_OK) {
                if ((*can_lock_held) != 0u) {
                    (void)power_lock_release(PM_LOCK_CAN_MONITORING);
                    (*can_lock_held) = 0u;
                }
                (*power_suspended) = 1u;
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_POWER_ACK_CAN);
            } else {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
        }
        app_runtime_mark_alive(GATEWAY_TASK_CAN);
        vTaskDelay(pdMS_TO_TICKS(20u));
        return 1;
    }
    if ((*power_suspended) != 0u) {
        status_t status = can_bus_resume(context->can_bus);

        (*power_suspended) = status == SYS_OK ? 0u : 1u;
        if (status != SYS_OK) {
            xEventGroupSetBits(channels.system_events,
                               SYSTEM_EVENT_FAULT_ACTIVE);
        }
        xEventGroupClearBits(channels.system_events,
                             SYSTEM_EVENT_POWER_ACK_CAN);
    }

    if ((*power_suspended) != 0u ||
        ((*can_lock_held) == 0u &&
         power_lock_acquire(PM_LOCK_CAN_MONITORING) != SYS_OK)) {
        xEventGroupSetBits(channels.system_events, SYSTEM_EVENT_FAULT_ACTIVE);
        app_runtime_mark_alive(GATEWAY_TASK_CAN);
        vTaskDelay(pdMS_TO_TICKS(20u));
        return 1;
    }
    (*can_lock_held) = 1u;
    return 0;
}

void can_task(void *argument)
{
    app_fieldbus_context_t *context = argument;
    uint8_t power_suspended = 0u;
    uint8_t can_lock_held = 0u;

    (void)xEventGroupWaitBits(channels.system_events,
                              SYSTEM_EVENT_FIELDBUS_READY,
                              pdFALSE,
                              pdTRUE,
                              portMAX_DELAY);
    if (power_lock_acquire(PM_LOCK_CAN_MONITORING) != SYS_OK) {
        xEventGroupSetBits(channels.system_events, SYSTEM_EVENT_FAULT_ACTIVE);
    } else {
        can_lock_held = 1u;
    }

    for (;;) {
        EventBits_t power_bits = xEventGroupGetBits(channels.system_events);

        if (handle_can_power(
                context, &power_suspended, &can_lock_held, power_bits)) {
            continue;
        }
        service_can_frames(context);
        app_runtime_mark_alive(GATEWAY_TASK_CAN);
    }
}
