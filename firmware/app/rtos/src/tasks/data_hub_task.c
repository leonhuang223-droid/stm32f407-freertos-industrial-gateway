/** @file data_hub_task.c
 * @author 兆鸣嵌入式
 * Owns the latest measurement and distributes resulting domain events.
 */
#include "runtime_internal.h"
#include <string.h>

#define DATA_HUB_CONFIG_WAIT_MS 100u

typedef struct {
    gateway_alarm_event_t events[ALARM_MAX_EVENTS_PER_MEASUREMENT];
    size_t count;
    uint32_t active_count;
    relay_state_t relay_state;
    status_t status;
} alarm_result_t;

static void evaluate_alarm(app_data_hub_task_context_t *context,
                           const gateway_measurement_t *measurement,
                           alarm_result_t *result)
{
    gateway_system_snapshot_t previous;

    memset(result, 0, sizeof(*result));
    result->relay_state = RELAY_DEENERGIZED;
    if (app_runtime_read_snapshot(&previous) == SYS_OK) {
        result->active_count = previous.active_alarm_count;
        result->relay_state = previous.relay_energized != 0u
                                  ? RELAY_ENERGIZED
                                  : RELAY_DEENERGIZED;
    }
    if (xSemaphoreTake(channels.config_mutex,
                       pdMS_TO_TICKS(DATA_HUB_CONFIG_WAIT_MS)) == pdPASS) {
        result->status =
            alarm_subsystem_process(context->alarm,
                                    measurement,
                                    result->events,
                                    ALARM_MAX_EVENTS_PER_MEASUREMENT,
                                    &result->count);
        result->active_count = alarm_subsystem_active_count(context->alarm);
        (void)relay_get_state(context->relay, &result->relay_state);
        xSemaphoreGive(channels.config_mutex);
    } else {
        result->status = ERR_TIMEOUT;
    }
    if (result->status != SYS_OK && result->status != ERR_UNSUPPORTED) {
        xEventGroupSetBits(channels.system_events, SYSTEM_EVENT_FAULT_ACTIVE);
    }
    if (result->active_count != 0u) {
        xEventGroupSetBits(channels.system_events, SYSTEM_EVENT_ALARM_ACTIVE);
    } else if (result->status == SYS_OK || result->status == ERR_UNSUPPORTED) {
        xEventGroupClearBits(channels.system_events, SYSTEM_EVENT_ALARM_ACTIVE);
    }
}

static uint32_t publish_snapshot(app_data_hub_task_context_t *context,
                                 const gateway_measurement_t *measurement,
                                 const alarm_result_t *alarm)
{
    uint32_t sequence = 0u;
    if (xSemaphoreTake(channels.snapshot_mutex, pdMS_TO_TICKS(10u)) == pdPASS) {
        context->snapshot->sequence++;
        context->snapshot->latest = *measurement;
        context->snapshot->active_alarm_count = alarm->active_count;
        context->snapshot->system_flags =
            (uint32_t)xEventGroupGetBits(channels.system_events);
        context->snapshot->relay_energized =
            alarm->relay_state == RELAY_ENERGIZED ? 1u : 0u;
        sequence = context->snapshot->sequence;
        xQueueOverwrite(channels.ui_snapshot, context->snapshot);
        xSemaphoreGive(channels.snapshot_mutex);
    } else {
        xEventGroupSetBits(channels.system_events, SYSTEM_EVENT_FAULT_ACTIVE);
    }
    return sequence;
}

static void distribute_measurement(app_data_hub_task_context_t *context,
                                   const gateway_measurement_t *measurement,
                                   uint32_t sequence)
{
    gateway_network_event_t network_event = {0};
    gateway_storage_log_request_t storage_log;
    gateway_can_tx_message_t can_message;

    network_event.type = GATEWAY_NETWORK_TELEMETRY;
    network_event.sequence = sequence;
    network_event.qos = 1u;
    network_event.payload.measurement = *measurement;
    xQueueOverwrite(channels.network_telemetry, &network_event);
    storage_log.sequence = sequence;
    storage_log.measurement = *measurement;
    if (xQueueSend(channels.storage_log, &storage_log, 0u) != pdPASS) {
        (*context->storage_log_publish_drops)++;
    }
    if (measurement->source != GATEWAY_SOURCE_CAN) {
        can_message.request_id = sequence;
        can_message.measurement = *measurement;
        if (app_rtos_submit_can_message(&can_message) != SYS_OK) {
            (*context->can_tx_publish_drops)++;
        }
    }
}

static void distribute_alarms(app_data_hub_task_context_t *context,
                              const alarm_result_t *alarm)
{
    size_t i;
    for (i = 0u; i < alarm->count; ++i) {
        gateway_storage_alarm_request_t storage_alarm;
        gateway_network_event_t network_event = {0};
        storage_alarm.event = alarm->events[i];
        if (xQueueSend(channels.storage_alarm,
                       &storage_alarm,
                       pdMS_TO_TICKS(5u)) != pdPASS) {
            (*context->storage_alarm_publish_drops)++;
            xEventGroupSetBits(channels.system_events,
                               SYSTEM_EVENT_FAULT_ACTIVE);
        }
        network_event.type = GATEWAY_NETWORK_ALARM;
        network_event.sequence = alarm->events[i].event_id;
        network_event.qos = 1u;
        network_event.payload.alarm = alarm->events[i];
        if (xQueueSend(channels.network_alarm,
                       &network_event,
                       pdMS_TO_TICKS(5u)) != pdPASS) {
            (*context->network_alarm_publish_drops)++;
        }
    }
}

void data_hub_task(void *argument)
{
    app_data_hub_task_context_t *context = argument;
    gateway_measurement_t measurement;

    for (;;) {
        if (xQueueReceive(channels.measurement,
                          &measurement,
                          pdMS_TO_TICKS(1000u)) == pdPASS) {
            alarm_result_t alarm;
            uint32_t sequence;
            evaluate_alarm(context, &measurement, &alarm);
            sequence = publish_snapshot(context, &measurement, &alarm);
            distribute_measurement(context, &measurement, sequence);
            distribute_alarms(context, &alarm);
        }
        app_runtime_mark_alive(GATEWAY_TASK_DATA_HUB);
    }
}
