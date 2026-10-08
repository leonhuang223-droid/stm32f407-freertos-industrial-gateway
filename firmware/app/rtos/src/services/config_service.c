#include "runtime_internal.h"
#include <stdio.h>
#include <string.h>

static app_config_service_context_t *service_context;
void app_config_service_bind(app_config_service_context_t *context)
{
    service_context = context;
}

status_t submit_config_patch(const config_patch_t *patch, uint32_t *request_id)
{
    app_config_service_context_t *context = service_context;
    gateway_storage_config_request_t request;
    status_t status;

    if (context == 0 || patch == 0) {
        return ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(channels.config_mutex, pdMS_TO_TICKS(50u)) != pdPASS) {
        return ERR_TIMEOUT;
    }
    if (alarm_subsystem_active_count(context->alarm) != 0u) {
        status = ERR_DEVICE_NOT_READY;
    } else {
        status = config_subsystem_prepare(context->config, patch, &request);
    }
    if (status == SYS_OK) {
        /* Publish or reject while holding the same transaction lock. A failed
         * enqueue cannot leave an unowned pending configuration behind. */
        status = xQueueSend(channels.storage_config, &request, 0u) == pdPASS
                     ? SYS_OK
                     : ERR_QUEUE_FULL;
        if (status != SYS_OK) {
            (void)config_subsystem_reject(
                context->config, request.request_id, status);
        }
    }
    xSemaphoreGive(channels.config_mutex);
    if (status != SYS_OK) {
        return status;
    }
    if (request_id != 0) {
        *request_id = request.request_id;
    }
    return SYS_OK;
}

status_t app_config_read(gateway_runtime_config_t *config,
                         config_health_t *health,
                         uint32_t timeout_ms)
{
    status_t status;
    if (service_context == 0 || config == 0) {
        return ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(channels.config_mutex, pdMS_TO_TICKS(timeout_ms)) !=
        pdPASS) {
        return ERR_TIMEOUT;
    }
    status = config_subsystem_get(service_context->config, config);
    if (status == SYS_OK && health != 0) {
        status = config_subsystem_get_health(service_context->config, health);
    }
    xSemaphoreGive(channels.config_mutex);
    return status;
}

status_t
app_config_persist_request(const gateway_storage_config_request_t *request,
                           config_persist_fn persist,
                           void *persist_context)
{
    status_t status;
    if (service_context == 0 || request == 0 || persist == 0) {
        return ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(channels.config_mutex, portMAX_DELAY) != pdPASS) {
        return ERR_TIMEOUT;
    }
    status = config_transaction_execute(service_context->config,
                                        service_context->alarm,
                                        request,
                                        persist,
                                        persist_context);
    xSemaphoreGive(channels.config_mutex);
    return status;
}

status_t acknowledge_alarm(uint32_t event_id)
{
    app_config_service_context_t *context = service_context;
    gateway_alarm_event_t event;
    gateway_storage_alarm_request_t request;
    status_t status;

    if (context == 0 || event_id == 0u) {
        return ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(channels.config_mutex, pdMS_TO_TICKS(50u)) != pdPASS) {
        return ERR_TIMEOUT;
    }
    status = alarm_subsystem_acknowledge(context->alarm, event_id, &event);
    xSemaphoreGive(channels.config_mutex);
    if (status != SYS_OK) {
        return status;
    }
    request.event = event;
    return xQueueSend(channels.storage_alarm, &request, pdMS_TO_TICKS(20u)) ==
                   pdPASS
               ? SYS_OK
               : ERR_QUEUE_FULL;
}
