#include "runtime_internal.h"
#include <stdio.h>
#include <string.h>

static ota_rtos_port_t ota_rtos_port;

static status_t wait_for_network_control_result(
    const gateway_network_control_request_t *request, TickType_t timeout_ticks);
static status_t process_boot_confirmation(app_ota_task_context_t *context);
static status_t prepare_ota_manager(app_ota_task_context_t *context);
static status_t set_ota_network_lease(gateway_network_control_type_t type,
                                      uint32_t request_id);
static status_t run_ota_network_phase(app_ota_task_context_t *context,
                                      const gateway_ota_command_t *command);

static status_t process_boot_confirmation(app_ota_task_context_t *context)
{
    status_t status = ERR_DEVICE_NOT_READY;

    if (context->supervisor->health.healthy != 0u &&
        power_lock_acquire(PM_LOCK_FLASH_WRITE) == SYS_OK) {
        status = boot_confirmation_confirm(context->boot_confirmation);
        (void)power_lock_release(PM_LOCK_FLASH_WRITE);
    }
    (*context->boot_confirmation_queued) = 0u;
    if (status != SYS_OK && status != ERR_DEVICE_NOT_READY) {
        xEventGroupSetBits(channels.system_events,
                           SYSTEM_EVENT_FAULT_ACTIVE);
    }
    return status;
}

void ota_poll_cancel_commands(ota_rtos_port_t *port)
{
    gateway_ota_command_t command;

    while (xQueueReceive(channels.ota_command, &command, 0u) == pdPASS) {
        if (command.type == GATEWAY_OTA_COMMAND_CANCEL) {
            port->cancel_seen = 1u;
            if (port->context->ota->initialized != 0u) {
                (void)ota_manager_abort(port->context->ota);
            }
        } else if (command.type == GATEWAY_OTA_COMMAND_CONFIRM_BOOT) {
            (void)process_boot_confirmation(port->context);
        }
    }
}

static status_t prepare_ota_manager(app_ota_task_context_t *context)
{
    ota_manager_port_t port;
    ota_manager_config_t config;
    boot_metadata_t metadata;
    app_slot_t copy_slot;
    status_t status;

    status = boot_meta_load(&context->boot_confirmation->store,
                            &metadata, &copy_slot);
    if (status != SYS_OK) {
        return status;
    }
    (void)copy_slot;
    memset(&port, 0, sizeof(port));
    ota_rtos_port.context = context;
    ota_rtos_port.cancel_seen = 0u;
    port.fetch_manifest = ota_port_fetch_manifest;
    port.http_open = ota_port_http_open;
    port.http_read = ota_port_http_read;
    port.http_close = ota_port_http_close;
    port.staging_begin = ota_port_staging_begin;
    port.staging_write = ota_port_staging_write;
    port.metadata_load = ota_port_metadata_load;
    port.metadata_commit = ota_port_metadata_commit;
    port.ota_metadata_commit = ota_port_staging_metadata_commit;
    port.enter_critical = ota_port_enter_critical;
    port.exit_critical = ota_port_exit_critical;
    port.context = &ota_rtos_port;

    config.manifest_url = (*context->ota_manifest_url);
    config.target_id = PROJECT_TARGET_ID;
    config.current_version = metadata.active_version;
    config.current_bootloader_version = BOOTLOADER_VERSION;
    return ota_manager_construct(context->ota, &port, &config);
}

static status_t set_ota_network_lease(gateway_network_control_type_t type,
                                      uint32_t request_id)
{
    gateway_network_control_request_t request;
    status_t status;

    request.type = type;
    request.request_id = request_id;
    request.deadline_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) + 5000u;
    status = app_rtos_submit_network_control(&request);
    if (status == SYS_OK) {
        status = wait_for_network_control_result(
            &request, pdMS_TO_TICKS(5000u));
    }
    return status;
}

static status_t run_ota_network_phase(app_ota_task_context_t *context,
                                      const gateway_ota_command_t *command)
{
    status_t status;
    status_t release_status;

    status = set_ota_network_lease(GATEWAY_NETWORK_CONTROL_OTA_ACQUIRE,
                                   command->request_id);
    if (status != SYS_OK) {
        return status;
    }
    status = prepare_ota_manager(context);
    if (status == SYS_OK) {
        status = ota_manager_check(context->ota);
    }
    if (status == SYS_OK && command->type == GATEWAY_OTA_COMMAND_START) {
        status = ota_manager_download(context->ota);
    }
    release_status = set_ota_network_lease(
        GATEWAY_NETWORK_CONTROL_OTA_RELEASE, command->request_id);
    if (status == SYS_OK && release_status != SYS_OK) {
        status = release_status;
    }
    return status;
}

void ota_task(void *argument)
{
    app_ota_task_context_t *context = argument;
    gateway_ota_command_t command;

    for (;;) {
        if (xQueueReceive(channels.ota_command, &command,
                          pdMS_TO_TICKS(1000U)) == pdPASS) {
            if (command.type == GATEWAY_OTA_COMMAND_CONFIRM_BOOT) {
                (void)process_boot_confirmation(context);
            } else if (command.type == GATEWAY_OTA_COMMAND_CHECK ||
                       command.type == GATEWAY_OTA_COMMAND_START) {
                status_t status = run_ota_network_phase(context, &command);

                if (status != SYS_OK && status != ERR_OTA_ABORTED) {
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_FAULT_ACTIVE);
                }
            } else if (command.type == GATEWAY_OTA_COMMAND_APPLY) {
                status_t status = context->ota->initialized != 0u
                    ? ota_manager_commit_pending(context->ota)
                    : ERR_DEVICE_NOT_READY;

                if (status != SYS_OK) {
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_FAULT_ACTIVE);
                }
            } else if (command.type == GATEWAY_OTA_COMMAND_CANCEL) {
                if (context->ota->initialized != 0u) {
                    (void)ota_manager_abort(context->ota);
                }
                (void)set_ota_network_lease(
                    GATEWAY_NETWORK_CONTROL_OTA_RELEASE,
                    command.request_id);
            }
        }
        app_runtime_mark_alive(GATEWAY_TASK_OTA);
    }
}

static status_t wait_for_network_control_result(
    const gateway_network_control_request_t *request,
    TickType_t timeout_ticks)
{
    TickType_t start = xTaskGetTickCount();

    for (;;) {
        gateway_network_control_result_t result;
        TickType_t elapsed = xTaskGetTickCount() - start;
        TickType_t remaining;
        if (elapsed >= timeout_ticks) {
            return ERR_TIMEOUT;
        }
        remaining = timeout_ticks - elapsed;
        if (remaining > pdMS_TO_TICKS(OTA_IO_WAIT_SLICE_MS)) {
            remaining = pdMS_TO_TICKS(OTA_IO_WAIT_SLICE_MS);
        }
        if (xQueueReceive(channels.network_control_result, &result,
                          remaining) != pdPASS) {
            app_runtime_mark_alive(GATEWAY_TASK_OTA);
            continue;
        }
        if (result.request_id == request->request_id &&
            result.type == request->type) {
            return result.status;
        }
        app_runtime_mark_alive(GATEWAY_TASK_OTA);
    }
}
