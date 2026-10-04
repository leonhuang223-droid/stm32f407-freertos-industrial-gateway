#include "runtime_internal.h"
#include <stdio.h>
#include <string.h>

void acquisition_task(void *argument)
{
    app_acquisition_task_context_t *context = argument;
    TickType_t next_wake = xTaskGetTickCount();
    const TickType_t period_ticks = pdMS_TO_TICKS(100u);
    const uint32_t period_ms =
        (uint32_t)(period_ticks * portTICK_PERIOD_MS);
    gateway_measurement_t measurements[ACQUISITION_MEASUREMENT_COUNT];
    uint8_t power_suspended = 0u;

    (void)periodic_timing_monitor_construct(
        context->acquisition_timing, period_ms,
        (uint32_t)(2u * portTICK_PERIOD_MS));
    for (;;) {
        TickType_t actual_release = xTaskGetTickCount();
        uint32_t now_ms =
            (uint32_t)(actual_release * portTICK_PERIOD_MS);
        EventBits_t power_bits = xEventGroupGetBits(channels.system_events);

        if ((power_bits & SYSTEM_EVENT_POWER_QUIESCE_REQUEST) != 0u) {
            if (power_suspended == 0u) {
                status_t status =
                    ((*context->initialization_mask) &
                     APP_INITIALIZED_ACQUISITION) != 0u
                        ? acquisition_subsystem_suspend(
                              context->acquisition)
                        : SYS_OK;

                if (status == SYS_OK) {
                    power_suspended = 1u;
                    xEventGroupSetBits(channels.system_events,
                        SYSTEM_EVENT_POWER_ACK_ACQUISITION);
                } else {
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_FAULT_ACTIVE);
                }
            }
            app_runtime_mark_alive(GATEWAY_TASK_ACQUISITION);
            vTaskDelay(pdMS_TO_TICKS(20u));
            continue;
        }
        if (power_suspended != 0u) {
            status_t status =
                ((*context->initialization_mask) &
                 APP_INITIALIZED_ACQUISITION) != 0u
                    ? acquisition_subsystem_resume(context->acquisition)
                    : SYS_OK;

            xEventGroupClearBits(channels.system_events,
                SYSTEM_EVENT_POWER_ACK_ACQUISITION);
            power_suspended = 0u;
            next_wake = actual_release;
            if (status != SYS_OK) {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
        }

        (void)periodic_timing_monitor_note(
            context->acquisition_timing, now_ms,
            (uint32_t)(next_wake * portTICK_PERIOD_MS));
        if (((*context->initialization_mask) & APP_INITIALIZED_ACQUISITION) != 0u) {
            size_t count = 0u;
            size_t i;

            (void)acquisition_subsystem_process(
                context->acquisition, now_ms,
                measurements, ACQUISITION_MEASUREMENT_COUNT, &count);
            for (i = 0u; i < count; ++i) {
                if (app_rtos_publish_measurement(&measurements[i]) != SYS_OK) {
                    (*context->measurement_publish_drops)++;
                }
            }
        }
        app_runtime_mark_alive(GATEWAY_TASK_ACQUISITION);
        vTaskDelayUntil(&next_wake, period_ticks);
    }
}
