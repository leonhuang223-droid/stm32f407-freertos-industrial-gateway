#include "runtime_internal.h"
#include <stdio.h>
#include <string.h>

static void process_ota_storage_request(app_storage_task_context_t *context,
                                        ota_storage_request_t *request);
static status_t persist_runtime_config(void *context,
    const gateway_storage_config_request_t *request);

static void process_ota_storage_request(app_storage_task_context_t *context,
                                        ota_storage_request_t *request)
{
    uint8_t needs_flash_lock;

    if (request == 0) {
        return;
    }
    if (request_lifecycle_begin(&request->lifecycle, request->lifecycle.generation,
            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS)) != SYS_OK) {
        notify_ota_waiter(request->waiter);
        return;
    }
    request->complete = 0u;
    needs_flash_lock = request->operation != OTA_STORAGE_BEGIN ? 1u : 0u;
    if (context->storage->health.mounted == 0u ||
        context->storage->health.powered_down != 0u) {
        request->status = ERR_DEVICE_NOT_READY;
    } else if (needs_flash_lock != 0u &&
               power_lock_acquire(PM_LOCK_FLASH_WRITE) != SYS_OK) {
        request->status = ERR_DEVICE_NOT_READY;
    } else {
        switch (request->operation) {
        case OTA_STORAGE_BEGIN:
            request->status = ota_staging_begin(
                context->ota_staging, request->length);
            break;
        case OTA_STORAGE_ERASE_NEXT:
            request->status = ota_staging_erase_next(
                context->ota_staging, &request->complete);
            break;
        case OTA_STORAGE_WRITE:
            request->status = ota_staging_write(
                context->ota_staging, request->offset,
                request->data, request->length);
            break;
        case OTA_STORAGE_COMMIT_METADATA:
            request->status = ota_staging_commit_metadata(
                context->ota_staging, request->data, request->length);
            break;
        default:
            request->status = ERR_UNSUPPORTED;
            break;
        }
        if (needs_flash_lock != 0u) {
            (void)power_lock_release(PM_LOCK_FLASH_WRITE);
        }
    }
    ota_storage_request_complete(request, request->status);
}

static status_t persist_runtime_config(void *context,
    const gateway_storage_config_request_t *request)
{
    return context != 0
        ? storage_subsystem_save_config(context, request) : ERR_DEVICE_NOT_READY;
}

void app_storage_task_step(app_storage_task_context_t *context,
                            app_storage_task_state_t *state, uint32_t wait_ms)
{
    uint32_t log_ready = 0u;
    uint32_t alarm_ready = 0u;
    uint32_t config_ready = 0u;
    uint32_t ota_ready = 0u;
    uint8_t flash_lock_held = 0u;
    uint8_t had_activity = 0u;
    QueueSetMemberHandle_t ready;
    uint32_t now_ms =
        (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    EventBits_t power_bits = xEventGroupGetBits(channels.system_events);
    power_mode_t current_power_mode;

    app_critical_enter();
    current_power_mode = context->power->current_mode;
    app_critical_exit();

    if ((power_bits & SYSTEM_EVENT_POWER_QUIESCE_REQUEST) != 0u) {
        if (state->power_suspended == 0u) {
            status_t status = SYS_OK;

            if (context->storage->health.mounted != 0u &&
                power_lock_acquire(PM_LOCK_FLASH_WRITE) ==
                    SYS_OK) {
                status = storage_subsystem_power_down(
                    context->storage);
                (void)power_lock_release(PM_LOCK_FLASH_WRITE);
            } else if (context->storage->health.mounted != 0u) {
                status = ERR_DEVICE_NOT_READY;
            }
            if (status == SYS_OK) {
                state->power_suspended = 1u;
                xEventGroupSetBits(channels.system_events,
                    SYSTEM_EVENT_POWER_ACK_STORAGE);
            } else {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
        }
        app_runtime_mark_alive(GATEWAY_TASK_STORAGE);
        vTaskDelay(pdMS_TO_TICKS(20u));
        return;
    }
    if (state->power_suspended != 0u) {
        status_t status = SYS_OK;

        if (context->storage->health.mounted != 0u &&
            power_lock_acquire(PM_LOCK_FLASH_WRITE) == SYS_OK) {
            status = storage_subsystem_wake(context->storage);
            (void)power_lock_release(PM_LOCK_FLASH_WRITE);
        } else if (context->storage->health.mounted != 0u) {
            status = ERR_DEVICE_NOT_READY;
        }
        xEventGroupClearBits(channels.system_events,
                             SYSTEM_EVENT_POWER_ACK_STORAGE);
        state->power_suspended = 0u;
        state->last_activity_ms = now_ms;
        if (status != SYS_OK) {
            xEventGroupSetBits(channels.system_events,
                               SYSTEM_EVENT_FAULT_ACTIVE);
        }
    }

    if (context->storage->health.mounted == 0u &&
        (state->mount_attempted == 0u || now_ms - state->last_mount_ms >= 1000u)) {
        state->mount_attempted = 1u;
        state->last_mount_ms = now_ms;
        if (power_lock_acquire(PM_LOCK_FLASH_WRITE) == SYS_OK) {
            (*context->storage_startup_status) =
                storage_subsystem_start(context->storage);
            (void)power_lock_release(PM_LOCK_FLASH_WRITE);
        } else {
            (*context->storage_startup_status) = ERR_DEVICE_NOT_READY;
        }
        if ((*context->storage_startup_status) == SYS_OK) {
            xEventGroupSetBits(channels.system_events,
                               SYSTEM_EVENT_STORAGE_READY);
        } else {
            xEventGroupClearBits(channels.system_events,
                                 SYSTEM_EVENT_STORAGE_READY);
            /* Keep servicing the queue set while storage is offline. */
        }
    }

    if (context->storage->health.mounted != 0u &&
        state->fault_archive_complete == 0u &&
        ((*context->initialization_mask) & APP_INITIALIZED_RELIABILITY) !=
            0u) {
        fault_record_t record;
        status_t fault_status = fault_recorder_get(
            context->fault_recorder, &record);

        if (fault_status == ERR_DEVICE_NOT_READY) {
            state->fault_archive_complete = 1u;
        } else if (fault_status == SYS_OK &&
                   power_lock_acquire(PM_LOCK_FLASH_WRITE) ==
                       SYS_OK) {
            if (xSemaphoreTake(channels.config_mutex,
                               portMAX_DELAY) == pdPASS) {
                fault_status = storage_subsystem_archive_fault(
                    context->storage, &record);
                xSemaphoreGive(channels.config_mutex);
            } else {
                fault_status = ERR_TIMEOUT;
            }
            (void)power_lock_release(PM_LOCK_FLASH_WRITE);
            if (fault_status == SYS_OK) {
                state->fault_archive_complete = 1u;
            } else {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
        }
    }

    ready = xQueueSelectFromSet(channels.storage_set,
                                pdMS_TO_TICKS(wait_ms));
    had_activity = ready != 0 ? 1u : 0u;
    if (had_activity != 0u &&
        context->storage->health.powered_down != 0u) {
        status_t wake_status = ERR_DEVICE_NOT_READY;

        if (power_lock_acquire(PM_LOCK_FLASH_WRITE) == SYS_OK) {
            wake_status = storage_subsystem_wake(context->storage);
            (void)power_lock_release(PM_LOCK_FLASH_WRITE);
        }
        if (wake_status != SYS_OK) {
            xEventGroupSetBits(channels.system_events,
                               SYSTEM_EVENT_FAULT_ACTIVE);
            /* Consume every selected member even if waking failed. */
        }
    }
    while (ready != 0) {
        if (ready == channels.storage_alarm) {
            alarm_ready++;
        } else if (ready == channels.storage_config) {
            config_ready++;
        } else if (ready == channels.storage_log) {
            log_ready++;
        } else if (ready == channels.ota_storage_request) {
            ota_ready++;
        }
        ready = xQueueSelectFromSet(channels.storage_set, 0u);
    }

    while (ota_ready != 0u) {
        ota_storage_request_t *request = 0;

        ota_ready--;
        if (xQueueReceive(channels.ota_storage_request, &request,
                          0u) == pdPASS) {
            process_ota_storage_request(context, request);
            app_runtime_mark_alive(GATEWAY_TASK_STORAGE);
        }
    }

    if (alarm_ready != 0u || config_ready != 0u || log_ready != 0u) {
        if (context->storage->health.mounted != 0u &&
            context->storage->health.powered_down == 0u &&
            power_lock_acquire(PM_LOCK_FLASH_WRITE) == SYS_OK) {
            flash_lock_held = 1u;
        } else {
            xEventGroupSetBits(channels.system_events,
                               SYSTEM_EVENT_FAULT_ACTIVE);
        }
    }

    while (alarm_ready != 0u) {
        gateway_storage_alarm_request_t request;

        alarm_ready--;
        if (xQueueReceive(channels.storage_alarm, &request, 0u) ==
                pdPASS &&
            (flash_lock_held == 0u ||
             storage_subsystem_append_alarm(context->storage,
                                            &request) != SYS_OK)) {
            xEventGroupSetBits(channels.system_events,
                               SYSTEM_EVENT_FAULT_ACTIVE);
        }
    }
    while (config_ready != 0u) {
        gateway_storage_config_request_t request;
        status_t status = ERR_IO;

        config_ready--;
        if (xQueueReceive(channels.storage_config, &request, 0u) !=
            pdPASS) {
            continue;
        }
        status = app_config_persist_request(&request, persist_runtime_config,
            flash_lock_held != 0u ? context->storage : 0);
        if (status != SYS_OK) {
            (*context->storage_config_publish_drops)++;
            xEventGroupSetBits(channels.system_events,
                               SYSTEM_EVENT_FAULT_ACTIVE);
        }
    }
    while (log_ready != 0u) {
        gateway_storage_log_request_t request;

        log_ready--;
        if (xQueueReceive(channels.storage_log, &request, 0u) ==
                pdPASS &&
            (flash_lock_held == 0u ||
             storage_subsystem_append_log(context->storage,
                                          &request) != SYS_OK)) {
            (*context->storage_log_publish_drops)++;
        }
    }
    if (flash_lock_held != 0u) {
        (void)power_lock_release(PM_LOCK_FLASH_WRITE);
    }
    if (had_activity != 0u) {
        state->last_activity_ms = (uint32_t)(xTaskGetTickCount() *
                                      portTICK_PERIOD_MS);
    } else if (current_power_mode == POWER_ECO &&
               context->storage->health.mounted != 0u &&
               context->storage->health.powered_down == 0u &&
               now_ms - state->last_activity_ms >= STORAGE_ECO_IDLE_MS &&
               (power_bits & SYSTEM_EVENT_OTA_ACTIVE) == 0u &&
               power_lock_acquire(PM_LOCK_FLASH_WRITE) ==
                   SYS_OK) {
        status_t power_status = storage_subsystem_power_down(
            context->storage);

        (void)power_lock_release(PM_LOCK_FLASH_WRITE);
        if (power_status != SYS_OK) {
            xEventGroupSetBits(channels.system_events,
                               SYSTEM_EVENT_FAULT_ACTIVE);
        }
    }
    app_runtime_mark_alive(GATEWAY_TASK_STORAGE);
}

void storage_task(void *argument)
{
    app_storage_task_context_t *context = argument;
    app_storage_task_state_t state = { 0 };
    state.last_activity_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    for (;;) {
        app_storage_task_step(context, &state, 1000u);
    }
}
