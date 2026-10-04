#include "runtime_internal.h"
#include <stdio.h>
#include <string.h>

void data_hub_task(void *argument)
{
    app_data_hub_task_context_t *context = argument;
    gateway_measurement_t measurement;

    for (;;) {
        if (xQueueReceive(channels.measurement, &measurement,
                          pdMS_TO_TICKS(1000U)) == pdPASS) {
            gateway_alarm_event_t alarm_events[
                ALARM_MAX_EVENTS_PER_MEASUREMENT];
            gateway_can_tx_message_t can_message;
            gateway_network_event_t network_event;
            gateway_storage_log_request_t storage_log;
            relay_state_t relay_state = RELAY_DEENERGIZED;
            size_t alarm_count = 0u;
            uint32_t active_alarm_count = 0u;
            size_t i;
            status_t alarm_status;

            if (xSemaphoreTake(channels.config_mutex,
                               portMAX_DELAY) == pdPASS) {
                alarm_status = alarm_subsystem_process(
                    context->alarm, &measurement, alarm_events,
                    ALARM_MAX_EVENTS_PER_MEASUREMENT, &alarm_count);
                active_alarm_count =
                    alarm_subsystem_active_count(context->alarm);
                (void)relay_get_state(context->relay, &relay_state);
                xSemaphoreGive(channels.config_mutex);
            } else {
                alarm_status = ERR_TIMEOUT;
                active_alarm_count = context->snapshot->active_alarm_count;
            }
            if (alarm_status != SYS_OK &&
                alarm_status != ERR_UNSUPPORTED) {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
            if (xSemaphoreTake(channels.snapshot_mutex,
                               pdMS_TO_TICKS(10U)) == pdPASS) {
                context->snapshot->sequence++;
                context->snapshot->latest = measurement;
                context->snapshot->active_alarm_count = active_alarm_count;
                context->snapshot->system_flags =
                    (uint32_t)xEventGroupGetBits(channels.system_events);
                context->snapshot->relay_energized =
                    relay_state == RELAY_ENERGIZED ? 1u : 0u;
                xQueueOverwrite(channels.ui_snapshot, context->snapshot);
                xSemaphoreGive(channels.snapshot_mutex);
            }

            if (active_alarm_count != 0u) {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_ALARM_ACTIVE);
            } else if (alarm_status == SYS_OK ||
                       alarm_status == ERR_UNSUPPORTED) {
                xEventGroupClearBits(channels.system_events,
                                     SYSTEM_EVENT_ALARM_ACTIVE);
            }

            memset(&network_event, 0, sizeof(network_event));
            network_event.type = GATEWAY_NETWORK_TELEMETRY;
            network_event.sequence = context->snapshot->sequence;
            network_event.qos = 1u;
            network_event.payload.measurement = measurement;
            xQueueOverwrite(channels.network_telemetry, &network_event);

            storage_log.sequence = context->snapshot->sequence;
            storage_log.measurement = measurement;
            if (xQueueSend(channels.storage_log, &storage_log, 0U) !=
                pdPASS) {
                (*context->storage_log_publish_drops)++;
            }

            for (i = 0u; i < alarm_count; ++i) {
                gateway_storage_alarm_request_t storage_alarm;

                storage_alarm.event = alarm_events[i];
                if (xQueueSend(channels.storage_alarm, &storage_alarm,
                               pdMS_TO_TICKS(5u)) != pdPASS) {
                    (*context->storage_alarm_publish_drops)++;
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_FAULT_ACTIVE);
                }
                memset(&network_event, 0, sizeof(network_event));
                network_event.type = GATEWAY_NETWORK_ALARM;
                network_event.sequence = alarm_events[i].event_id;
                network_event.qos = 1u;
                network_event.payload.alarm = alarm_events[i];
                if (xQueueSend(channels.network_alarm, &network_event,
                               pdMS_TO_TICKS(5u)) !=
                    pdPASS) {
                    (*context->network_alarm_publish_drops)++;
                }
            }

            if (measurement.source != GATEWAY_SOURCE_CAN) {
                can_message.request_id = context->snapshot->sequence;
                can_message.measurement = measurement;
                if (app_rtos_submit_can_message(&can_message) != SYS_OK) {
                    (*context->can_tx_publish_drops)++;
                }
            }
        }
        app_runtime_mark_alive(GATEWAY_TASK_DATA_HUB);
    }
}
