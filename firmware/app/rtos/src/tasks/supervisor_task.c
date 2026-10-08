#include "runtime_internal.h"
#include <stdio.h>
#include <string.h>

static void process_power_commands(app_supervisor_task_context_t *context,
                                   uint32_t now_ms)
{
    power_command_t power_command;
    status_t status;

    while (xQueueReceive(channels.power_command, &power_command, 0u) ==
           pdPASS) {
        if (power_command.operation == POWER_COMMAND_REQUEST_STOP) {
            status = deep_power_controller_request(context->deep_power,
                                                   POWER_STOP_PERIODIC,
                                                   power_command.duration_ms,
                                                   DEEP_POWER_CONFIRMATION,
                                                   now_ms);
        } else if (power_command.operation == POWER_COMMAND_REQUEST_STANDBY) {
            status = deep_power_controller_request(context->deep_power,
                                                   POWER_STANDBY_SHIPPING,
                                                   0u,
                                                   DEEP_POWER_CONFIRMATION,
                                                   now_ms);
        } else {
            status = deep_power_controller_cancel(context->deep_power);
            xEventGroupClearBits(channels.system_events,
                                 SYSTEM_EVENT_POWER_QUIESCE_REQUEST);
        }
        if (status != SYS_OK && status != ERR_UNSUPPORTED &&
            status != ERR_DEVICE_NOT_READY) {
            supervisor_subsystem_latch_fault(context->supervisor, status);
        }
    }
}

static void service_deep_power(app_supervisor_task_context_t *context,
                               uint32_t now_ms,
                               EventBits_t bits)
{
    status_t status;

    if (context->deep_power->health.request_pending != 0u) {
        deep_power_health_t deep_health;

        xEventGroupSetBits(channels.system_events,
                           SYSTEM_EVENT_POWER_QUIESCE_REQUEST);
        bits = xEventGroupGetBits(channels.system_events);
        status = deep_power_controller_process(
            context->deep_power,
            deep_power_quiesced_mask(bits),
            power_manager_deepest_allowed(context->power),
            watchdog_device_remaining_ms(context->watchdog),
            now_ms);
        (void)deep_power_controller_get_health(context->deep_power,
                                               &deep_health);
        if (deep_health.request_pending == 0u) {
            xEventGroupClearBits(channels.system_events,
                                 SYSTEM_EVENT_POWER_QUIESCE_REQUEST);
        } else if (status != SYS_OK && status != ERR_DEVICE_NOT_READY &&
                   status != ERR_TIMEOUT) {
            (void)deep_power_controller_cancel(context->deep_power);
            xEventGroupClearBits(channels.system_events,
                                 SYSTEM_EVENT_POWER_QUIESCE_REQUEST);
        }
    } else if ((bits & SYSTEM_EVENT_POWER_QUIESCE_REQUEST) != 0u) {
        xEventGroupClearBits(channels.system_events,
                             SYSTEM_EVENT_POWER_QUIESCE_REQUEST);
    }
}

static void service_boot_confirmation(app_supervisor_task_context_t *context,
                                      uint32_t now_ms,
                                      uint8_t *boot_fault_latched)
{
    if (context->boot_confirmation->attempted == 0u &&
        (*context->boot_confirmation_queued) == 0u &&
        supervisor_subsystem_boot_confirm_ready(context->supervisor, now_ms) !=
            0u) {
        gateway_ota_command_t command;

        command.type = GATEWAY_OTA_COMMAND_CONFIRM_BOOT;
        command.request_id = now_ms;
        if (xQueueSend(channels.ota_command, &command, 0u) == pdPASS) {
            (*context->boot_confirmation_queued) = 1u;
        }
    }
    if ((*context->boot_confirmation_queued) == 0u &&
        context->boot_confirmation->attempted != 0u &&
        context->boot_confirmation->health.confirmed == 0u &&
        context->boot_confirmation->health.last_error != SYS_OK &&
        *boot_fault_latched == 0u) {
        supervisor_subsystem_latch_fault(
            context->supervisor, context->boot_confirmation->health.last_error);
        xEventGroupSetBits(channels.system_events, SYSTEM_EVENT_FAULT_ACTIVE);
        *boot_fault_latched = 1u;
    }
}

void supervisor_task(void *argument)
{
    app_supervisor_task_context_t *context = argument;
    TickType_t next_wake = xTaskGetTickCount();
    uint32_t next_diagnostics_ms = 0u;
    uint8_t alarm_lock_held = 0u;
    uint8_t boot_fault_latched = 0u;
    const uint32_t required_services =
        APP_INITIALIZED_ACQUISITION | APP_INITIALIZED_FIELDBUS |
        APP_INITIALIZED_STORAGE | APP_INITIALIZED_CONTROL |
        APP_INITIALIZED_NETWORK | APP_INITIALIZED_CONFIG | APP_INITIALIZED_UI |
        APP_INITIALIZED_CLI | APP_INITIALIZED_RELIABILITY;

    if (supervisor_subsystem_start(context->supervisor,
                                   (uint32_t)(next_wake * portTICK_PERIOD_MS),
                                   (*context->heartbeat)) != SYS_OK) {
        supervisor_subsystem_latch_fault(context->supervisor,
                                         ERR_DEVICE_NOT_READY);
    }

    for (;;) {
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        EventBits_t bits = xEventGroupGetBits(channels.system_events);
        uint8_t alarm_active =
            (bits & SYSTEM_EVENT_ALARM_ACTIVE) != 0u ? 1u : 0u;
        status_t status;

        process_power_commands(context, now_ms);

        if (alarm_active != 0u && alarm_lock_held == 0u) {
            if (power_lock_acquire(PM_LOCK_ALARM_ACTIVE) == SYS_OK) {
                alarm_lock_held = 1u;
            }
        } else if (alarm_active == 0u && alarm_lock_held != 0u) {
            if (power_lock_release(PM_LOCK_ALARM_ACTIVE) == SYS_OK) {
                alarm_lock_held = 0u;
            }
        }
        app_critical_enter();
        (void)power_manager_evaluate(context->power, now_ms, alarm_active);
        app_critical_exit();
        service_deep_power(context, now_ms, bits);

        status = supervisor_subsystem_process(
            context->supervisor,
            now_ms,
            (*context->heartbeat),
            ((*context->initialization_mask) & required_services) ==
                    required_services
                ? 1u
                : 0u);
        if (status == ERR_TIMEOUT) {
            xEventGroupSetBits(channels.system_events,
                               SYSTEM_EVENT_FAULT_ACTIVE);
        }
        service_boot_confirmation(context, now_ms, &boot_fault_latched);
        if (now_ms - next_diagnostics_ms >= 1000u) {
            collect_rtos_diagnostics();
            next_diagnostics_ms = now_ms;
        }
        app_runtime_mark_alive(GATEWAY_TASK_SUPERVISOR);
        vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(100U));
    }
}
