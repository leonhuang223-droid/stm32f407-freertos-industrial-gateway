#include "runtime_internal.h"
#include <stdio.h>
#include <string.h>

static app_runtime_support_context_t *service_context;
static uint32_t previous_runtime[GATEWAY_TASK_COUNT];
static uint32_t previous_idle_runtime;
static uint32_t previous_total_runtime;
static uint8_t runtime_baseline_valid;

void app_runtime_support_bind(app_runtime_support_context_t *context)
{
    service_context = context;
    memset(previous_runtime, 0, sizeof(previous_runtime));
    previous_idle_runtime = 0u;
    previous_total_runtime = 0u;
    runtime_baseline_valid = 0u;
}

uint32_t deep_power_quiesced_mask(EventBits_t bits)
{
    uint32_t mask = 0u;

    if ((bits & SYSTEM_EVENT_POWER_ACK_ACQUISITION) != 0u) {
        mask |= DEEP_POWER_PARTICIPANT_ACQUISITION;
    }
    if ((bits & SYSTEM_EVENT_POWER_ACK_MODBUS) != 0u) {
        mask |= DEEP_POWER_PARTICIPANT_MODBUS;
    }
    if ((bits & SYSTEM_EVENT_POWER_ACK_CAN) != 0u) {
        mask |= DEEP_POWER_PARTICIPANT_CAN;
    }
    if ((bits & SYSTEM_EVENT_POWER_ACK_NETWORK) != 0u) {
        mask |= DEEP_POWER_PARTICIPANT_NETWORK;
    }
    if ((bits & SYSTEM_EVENT_POWER_ACK_STORAGE) != 0u) {
        mask |= DEEP_POWER_PARTICIPANT_STORAGE;
    }
    if ((bits & SYSTEM_EVENT_POWER_ACK_UI) != 0u) {
        mask |= DEEP_POWER_PARTICIPANT_UI;
    }
    return mask;
}

void app_critical_enter(void)
{
    app_runtime_support_context_t *context = service_context;
    taskENTER_CRITICAL();
    if (context != 0) {
        (void)critical_timing_monitor_enter(
            context->critical_timing,
            portGET_RUN_TIME_COUNTER_VALUE());
    }
}

void app_critical_exit(void)
{
    app_runtime_support_context_t *context = service_context;
    if (context != 0) {
        (void)critical_timing_monitor_exit(
            context->critical_timing,
            portGET_RUN_TIME_COUNTER_VALUE());
    }
    taskEXIT_CRITICAL();
}

status_t power_lock_acquire(power_lock_id_t lock)
{
    app_runtime_support_context_t *context = service_context;
    status_t status;
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    app_critical_enter();
    status = power_manager_acquire(context->power, lock, now_ms);
    app_critical_exit();
    return status;
}

status_t power_lock_release(power_lock_id_t lock)
{
    app_runtime_support_context_t *context = service_context;
    status_t status;
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    app_critical_enter();
    status = power_manager_release(context->power, lock, now_ms);
    app_critical_exit();
    return status;
}

void collect_rtos_diagnostics(void)
{
    app_runtime_support_context_t *context = service_context;
    QueueHandle_t queues[15] = {
        channels.measurement,
        channels.can_tx,
        channels.ui_snapshot,
        channels.network_telemetry,
        channels.network_alarm,
        channels.network_control,
        channels.network_control_result,
        channels.ota_command,
        channels.storage_log,
        channels.storage_alarm,
        channels.storage_config,
        channels.ui_command,
        channels.ota_network_request,
        channels.ota_storage_request,
        channels.power_command
    };
    TaskStatus_t task_status[APP_RTOS_SYSTEM_TASK_CAPACITY];
    uint32_t current_runtime[GATEWAY_TASK_COUNT] = { 0u };
    uint32_t current_idle_runtime = 0u;
    uint32_t total_runtime = 0u;
    UBaseType_t status_count;
    unsigned int i;

    for (i = 0u; i < GATEWAY_TASK_COUNT; ++i) {
        context->rtos_diagnostics->stack_high_water[i] =
            task_handles[i] != 0
                ? (uint32_t)uxTaskGetStackHighWaterMark(task_handles[i])
                : 0u;
    }
    for (i = 0u; i < 15u; ++i) {
        UBaseType_t waiting = queues[i] != 0
            ? uxQueueMessagesWaiting(queues[i]) : 0u;

        context->rtos_diagnostics->queue_current[i] = (uint16_t)waiting;
        if (waiting > context->rtos_diagnostics->queue_high_water[i]) {
            context->rtos_diagnostics->queue_high_water[i] =
                (uint16_t)waiting;
        }
    }
    context->rtos_diagnostics->samples++;

    status_count = uxTaskGetSystemState(
        task_status, APP_RTOS_SYSTEM_TASK_CAPACITY, &total_runtime);
    if (status_count == 0u) {
        context->rtos_diagnostics->runtime_errors++;
        return;
    }
    for (i = 0u; i < status_count; ++i) {
        unsigned int task_index;

        if (strcmp(task_status[i].pcTaskName, "IDLE") == 0) {
            current_idle_runtime = (uint32_t)task_status[i].ulRunTimeCounter;
        }
        for (task_index = 0u; task_index < GATEWAY_TASK_COUNT;
             ++task_index) {
            if (task_status[i].xHandle == task_handles[task_index]) {
                current_runtime[task_index] =
                    (uint32_t)task_status[i].ulRunTimeCounter;
                break;
            }
        }
    }
    if (runtime_baseline_valid != 0u) {
        uint32_t total_delta = total_runtime - previous_total_runtime;
        uint32_t known_delta = 0u;
        uint32_t idle_delta =
            current_idle_runtime - previous_idle_runtime;

        if (total_delta == 0u) {
            context->rtos_diagnostics->runtime_errors++;
        } else {
            for (i = 0u; i < GATEWAY_TASK_COUNT; ++i) {
                uint32_t delta = current_runtime[i] - previous_runtime[i];
                uint32_t permille = (uint32_t)(
                    ((uint64_t)delta * 1000u + total_delta / 2u) /
                    total_delta);

                known_delta += delta;
                context->rtos_diagnostics->cpu_permille[i] =
                    (uint16_t)(permille > 1000u ? 1000u : permille);
            }
            context->rtos_diagnostics->idle_cpu_permille = (uint16_t)(
                ((uint64_t)idle_delta * 1000u + total_delta / 2u) /
                total_delta);
            if (context->rtos_diagnostics->idle_cpu_permille > 1000u) {
                context->rtos_diagnostics->idle_cpu_permille = 1000u;
            }
            context->rtos_diagnostics->system_cpu_permille =
                known_delta + idle_delta < total_delta
                    ? (uint16_t)(((uint64_t)(total_delta - known_delta -
                        idle_delta) * 1000u + total_delta / 2u) /
                        total_delta)
                    : 0u;
            context->rtos_diagnostics->runtime_samples++;
        }
    } else {
        runtime_baseline_valid = 1u;
    }
    for (i = 0u; i < GATEWAY_TASK_COUNT; ++i) {
        previous_runtime[i] = current_runtime[i];
    }
    previous_idle_runtime = current_idle_runtime;
    previous_total_runtime = total_runtime;
}

const char *task_name_from_token(uint32_t task_token)
{
    unsigned int i;

    for (i = 0u; i < GATEWAY_TASK_COUNT; ++i) {
        if ((uint32_t)(uintptr_t)task_handles[i] == task_token) {
            return app_runtime_task_name((gateway_task_id_t)i);
        }
    }
    return "unknown";
}

const char *power_mode_name(power_mode_t mode)
{
    static const char *const names[POWER_MODE_COUNT] = {
        "active", "eco", "tickless", "stop", "standby"
    };

    return (unsigned int)mode < POWER_MODE_COUNT ? names[mode] : "invalid";
}

const char *power_policy_name(power_policy_t policy)
{
    static const char *const names[] = { "auto", "active", "eco" };

    return (unsigned int)policy < sizeof(names) / sizeof(names[0])
        ? names[policy] : "invalid";
}

void app_runtime_mark_alive(gateway_task_id_t task)
{
    if (service_context != 0 && (unsigned int)task < GATEWAY_TASK_COUNT) {
        (*service_context->heartbeat)[task]++;
    }
}

status_t app_runtime_read_snapshot(gateway_system_snapshot_t *snapshot)
{
    if (service_context == 0 || snapshot == 0) {
        return ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(channels.snapshot_mutex, pdMS_TO_TICKS(10u)) != pdPASS) {
        return ERR_TIMEOUT;
    }
    *snapshot = *service_context->snapshot;
    xSemaphoreGive(channels.snapshot_mutex);
    return SYS_OK;
}
