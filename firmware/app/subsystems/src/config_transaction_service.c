#include "config_transaction_service.h"
#include <string.h>

status_t config_transaction_execute(config_subsystem_t *config,
    alarm_subsystem_t *alarm, const gateway_storage_config_request_t *request,
    config_persist_fn persist, void *persist_context)
{
    gateway_runtime_config_t previous;
    status_t status;
    if (config == 0 || alarm == 0 || request == 0 || persist == 0) {
        return ERR_INVALID_ARG;
    }
    /* A stale queue item must never change the relay or durable configuration. */
    if (config->health.pending_requests != 1u ||
        config->health.last_request_id != request->request_id ||
        memcmp(&config->staged, &request->config, sizeof(request->config)) != 0) {
        return ERR_INVALID_ARG;
    }
    previous = config->active;
    status = alarm_subsystem_reconfigure(alarm, &request->config);
    if (status == SYS_OK) {
        status = persist(persist_context, request);
        if (status != SYS_OK) {
            status_t rollback = alarm_subsystem_reconfigure(alarm, &previous);
            if (rollback != SYS_OK) {
                status = rollback;
            }
        }
    }
    if (status == SYS_OK) {
        status = config_subsystem_commit(config, request);
    } else {
        (void)config_subsystem_reject(config, request->request_id, status);
    }
    return status;
}
