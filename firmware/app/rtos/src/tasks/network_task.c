#include "runtime_internal.h"
#include <stdio.h>
#include <string.h>

static void process_ota_network_request(app_network_task_context_t *context,
                                        ota_network_request_t *request);

static void process_ota_network_request(app_network_task_context_t *context,
                                        ota_network_request_t *request)
{
    if (request == 0) {
        return;
    }
    status_t begin_status;
    taskENTER_CRITICAL();
    begin_status = request_lifecycle_begin(
        &request->lifecycle,
        request->lifecycle.generation,
        (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
    taskEXIT_CRITICAL();
    if (begin_status != SYS_OK) {
        notify_ota_waiter(request->waiter);
        return;
    }
    request->length = 0u;
    request->content_length = 0u;
    if (context->network->health.ota_lease_active == 0u) {
        request->status = ERR_DEVICE_NOT_READY;
    } else {
        if (request->operation != OTA_NETWORK_CLOSE_PACKAGE &&
            network_transport_is_initialized(context->network_transport) ==
                0u) {
            request->status =
                network_transport_init(context->network_transport);
            if (request->status != SYS_OK) {
                ota_network_request_complete(request, request->status);
                return;
            }
        }
        switch (request->operation) {
        case OTA_NETWORK_FETCH_MANIFEST:
            request->status = http_get_document(context->ota_http,
                                                request->url,
                                                request->buffer,
                                                request->capacity,
                                                &request->length);
            break;
        case OTA_NETWORK_OPEN_PACKAGE:
            request->status = http_open_get(context->ota_http, request->url);
            if (request->status == SYS_OK) {
                request->content_length =
                    context->ota_http->header.content_length;
            }
            break;
        case OTA_NETWORK_READ_PACKAGE:
            request->status = http_read_body_chunk(context->ota_http,
                                                   request->buffer,
                                                   request->capacity,
                                                   &request->length);
            break;
        case OTA_NETWORK_CLOSE_PACKAGE:
            request->status = http_close(context->ota_http);
            break;
        default:
            request->status = ERR_UNSUPPORTED;
            break;
        }
    }
    ota_network_request_complete(request, request->status);
}

void app_network_task_init(app_network_task_context_t *context,
                           app_network_task_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->configured =
        (*context->initialization_mask & APP_INITIALIZED_NETWORK) != 0u;
    state->http_status = ERR_DEVICE_NOT_READY;
    if (state->configured == pdTRUE) {
        state->http_status =
            http_client_construct(context->ota_http,
                                  context->network_transport,
                                  (*context->ota_http_timeout_ms));
        (*context->network_startup_status) = network_subsystem_start(
            context->network,
            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
    }
}

static int handle_network_power(app_network_task_context_t *context,
                                app_network_task_state_t *state,
                                uint32_t now_ms,
                                EventBits_t power_bits)
{
    if ((power_bits & SYSTEM_EVENT_POWER_QUIESCE_REQUEST) != 0u) {
        if (state->power_suspended == 0u) {
            status_t status =
                state->configured == pdTRUE
                    ? network_subsystem_suspend(context->network, now_ms)
                    : SYS_OK;

            if (status == SYS_OK) {
                state->power_suspended = 1u;
                xEventGroupClearBits(channels.system_events,
                                     SYSTEM_EVENT_NETWORK_UP |
                                         SYSTEM_EVENT_MQTT_READY);
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_POWER_ACK_NETWORK);
            } else if (status != ERR_DEVICE_NOT_READY) {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
        }
        app_runtime_mark_alive(GATEWAY_TASK_NETWORK);
        return 1;
    }
    if (state->power_suspended != 0u) {
        status_t status =
            state->configured == pdTRUE
                ? network_subsystem_resume(context->network, now_ms)
                : SYS_OK;

        xEventGroupClearBits(channels.system_events,
                             SYSTEM_EVENT_POWER_ACK_NETWORK);
        state->power_suspended = status == SYS_OK ? 0u : 1u;
        if (status != SYS_OK) {
            xEventGroupSetBits(channels.system_events,
                               SYSTEM_EVENT_FAULT_ACTIVE);
        }
    }

    return 0;
}

static void process_network_controls(app_network_task_context_t *context,
                                     app_network_task_state_t *state,
                                     uint32_t now_ms)
{
    gateway_network_control_request_t control;

    while (xQueueReceive(channels.network_control, &control, 0u) == pdPASS) {
        gateway_network_control_result_t result;

        result.type = control.type;
        result.request_id = control.request_id;
        if (control.type != GATEWAY_NETWORK_CONTROL_OTA_RELEASE &&
            control.deadline_ms != 0u &&
            (int32_t)((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) -
                      control.deadline_ms) >= 0) {
            result.status = ERR_TIMEOUT;
        } else if (state->configured != pdTRUE) {
            result.status = ERR_DEVICE_NOT_READY;
        } else if (control.type == GATEWAY_NETWORK_CONTROL_OTA_ACQUIRE) {
            result.status =
                network_subsystem_acquire_ota_lease(context->network, now_ms);
        } else if (control.type == GATEWAY_NETWORK_CONTROL_OTA_RELEASE) {
            result.status =
                network_subsystem_release_ota_lease(context->network, now_ms);
        } else {
            result.status = ERR_UNSUPPORTED;
        }
        if (result.status == SYS_OK &&
            control.type == GATEWAY_NETWORK_CONTROL_OTA_ACQUIRE &&
            control.deadline_ms != 0u &&
            (int32_t)((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) -
                      control.deadline_ms) >= 0) {
            (void)network_subsystem_release_ota_lease(context->network, now_ms);
            result.status = ERR_TIMEOUT;
        }
        if (xQueueSend(channels.network_control_result,
                       &result,
                       pdMS_TO_TICKS(20u)) != pdPASS) {
            (*context->network_control_publish_drops)++;
            if (result.status == SYS_OK &&
                control.type == GATEWAY_NETWORK_CONTROL_OTA_ACQUIRE) {
                (void)network_subsystem_release_ota_lease(context->network,
                                                          now_ms);
            }
        }
    }
}

static void service_mqtt(app_network_task_context_t *context,
                         app_network_task_state_t *state,
                         uint32_t now_ms)
{
    network_health_t health;

    if (state->configured == pdTRUE) {
        status_t process_status;

        if (power_lock_acquire(PM_LOCK_NETWORK_TX) == SYS_OK) {
            process_status =
                network_subsystem_process(context->network, now_ms);
            (void)power_lock_release(PM_LOCK_NETWORK_TX);
        } else {
            process_status = ERR_DEVICE_NOT_READY;
        }

        if (network_subsystem_get_health(context->network, &health) == SYS_OK) {
            (*context->network_startup_status) =
                health.mqtt_ready != 0u ? SYS_OK : process_status;
            if (health.mqtt_ready != 0u) {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_NETWORK_UP |
                                       SYSTEM_EVENT_MQTT_READY);
            } else {
                xEventGroupClearBits(channels.system_events,
                                     SYSTEM_EVENT_NETWORK_UP |
                                         SYSTEM_EVENT_MQTT_READY);
            }
            if (health.ota_lease_active != 0u) {
                if (state->ota_lock_held == 0u &&
                    power_lock_acquire(PM_LOCK_OTA) == SYS_OK) {
                    state->ota_lock_held = 1u;
                }
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_OTA_ACTIVE);
            } else {
                if (state->ota_lock_held != 0u &&
                    power_lock_release(PM_LOCK_OTA) == SYS_OK) {
                    state->ota_lock_held = 0u;
                }
                xEventGroupClearBits(channels.system_events,
                                     SYSTEM_EVENT_OTA_ACTIVE);
            }
        }
    }
}

void app_network_task_step(app_network_task_context_t *context,
                           app_network_task_state_t *state)
{
    gateway_network_event_t event;
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    EventBits_t power_bits = xEventGroupGetBits(channels.system_events);

    if (handle_network_power(context, state, now_ms, power_bits)) {
        return;
    }

    process_network_controls(context, state, now_ms);

    {
        ota_network_request_t *request = 0;

        while (xQueueReceive(channels.ota_network_request, &request, 0u) ==
               pdPASS) {
            if (state->http_status != SYS_OK && request != 0) {
                request->status = state->http_status;
                ota_network_request_complete(request, request->status);
            } else {
                process_ota_network_request(context, request);
            }
            app_runtime_mark_alive(GATEWAY_TASK_NETWORK);
        }
    }
    while (xQueueReceive(channels.network_alarm, &event, 0u) == pdPASS) {
        if (state->configured != pdTRUE ||
            network_subsystem_submit(context->network, &event) != SYS_OK) {
            (*context->network_alarm_publish_drops)++;
        }
    }
    if (xQueueReceive(channels.network_telemetry, &event, 0u) == pdPASS) {
        if (state->configured == pdTRUE) {
            (void)network_subsystem_submit(context->network, &event);
        }
    }
    service_mqtt(context, state, now_ms);
    app_runtime_mark_alive(GATEWAY_TASK_NETWORK);
}

void network_task(void *argument)
{
    app_network_task_context_t *context = argument;
    app_network_task_state_t state;
    app_network_task_init(context, &state);
    for (;;) {
        app_network_task_step(context, &state);
        vTaskDelay(pdMS_TO_TICKS(20u));
    }
}
