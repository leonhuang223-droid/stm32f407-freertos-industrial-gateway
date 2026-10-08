#include "runtime_internal.h"
#include <stdio.h>
#include <string.h>

static ota_network_request_t ota_network_mailbox;
static ota_storage_request_t ota_storage_mailbox;
static uint8_t ota_network_mailbox_busy;
static uint8_t ota_storage_mailbox_busy;
static uint8_t ota_network_io_buffer[OTA_MANAGER_MANIFEST_BUFFER_SIZE];
static char ota_network_io_url[MANIFEST_DOWNLOAD_URL_LEN];
static uint8_t ota_storage_io_buffer[OTA_MANAGER_MANIFEST_BUFFER_SIZE];

static void clear_ota_notification(void);
static status_t wait_for_ota_io(ota_rtos_port_t *port,
                                request_lifecycle_t *lifecycle);
static status_t submit_ota_network_request(ota_rtos_port_t *port,
                                           ota_network_request_t *request);
static status_t submit_ota_storage_request(ota_rtos_port_t *port,
                                           ota_storage_request_t *request);

void notify_ota_waiter(TaskHandle_t waiter)
{
    if (waiter != 0) {
        (void)xTaskNotify(waiter, OTA_IO_NOTIFY_DONE, eSetBits);
    }
}

static void clear_ota_notification(void)
{
    uint32_t ignored;

    (void)xTaskNotifyWait(0u, UINT32_MAX, &ignored, 0u);
}

static status_t wait_for_ota_io(ota_rtos_port_t *port,
                                request_lifecycle_t *lifecycle)
{
    for (;;) {
        uint32_t events = 0u;
        status_t status;
        taskENTER_CRITICAL();
        status = request_lifecycle_poll(
            lifecycle, (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
        if (status == ERR_TIMEOUT) {
            request_lifecycle_cancel(lifecycle);
        }
        taskEXIT_CRITICAL();
        if (status != ERR_IN_PROGRESS) {
            ota_poll_cancel_commands(port);
            return port->cancel_seen != 0u ? ERR_OTA_ABORTED : status;
        }
        (void)xTaskNotifyWait(
            0u, UINT32_MAX, &events, pdMS_TO_TICKS(OTA_IO_WAIT_SLICE_MS));
        ota_poll_cancel_commands(port);
        if (port->cancel_seen != 0u) {
            taskENTER_CRITICAL();
            request_lifecycle_cancel(lifecycle);
            taskEXIT_CRITICAL();
        }
        app_runtime_mark_alive(GATEWAY_TASK_OTA);
    }
}

void ota_network_request_complete(ota_network_request_t *request,
                                  status_t status)
{
    taskENTER_CRITICAL();
    (void)request_lifecycle_complete(
        &request->lifecycle, request->lifecycle.generation, status);
    taskEXIT_CRITICAL();
    notify_ota_waiter(request->waiter);
}

void ota_storage_request_complete(ota_storage_request_t *request,
                                  status_t status)
{
    taskENTER_CRITICAL();
    (void)request_lifecycle_complete(
        &request->lifecycle, request->lifecycle.generation, status);
    taskEXIT_CRITICAL();
    notify_ota_waiter(request->waiter);
}

static status_t submit_ota_network_request(ota_rtos_port_t *port,
                                           ota_network_request_t *request)
{
    ota_network_request_t *queued = &ota_network_mailbox;
    status_t status;
    if (ota_network_mailbox_busy != 0u &&
        queued->lifecycle.state != REQUEST_COMPLETED) {
        return ERR_DEVICE_NOT_READY;
    }
    if (request->capacity > sizeof(ota_network_io_buffer) ||
        (request->url != 0 &&
         strlen(request->url) >= sizeof(ota_network_io_url))) {
        return ERR_INVALID_ARG;
    }
    {
        request_lifecycle_t lifecycle = queued->lifecycle;
        *queued = *request;
        queued->lifecycle = lifecycle;
    }
    if (request->url != 0) {
        memcpy(ota_network_io_url, request->url, strlen(request->url) + 1u);
        queued->url = ota_network_io_url;
    }
    queued->buffer = ota_network_io_buffer;
    queued->waiter = xTaskGetCurrentTaskHandle();
    queued->status = ERR_DEVICE_NOT_READY;
    (void)request_lifecycle_submit(
        &queued->lifecycle,
        (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
        OTA_NETWORK_IO_TIMEOUT_MS);
    clear_ota_notification();
    ota_network_mailbox_busy = 1u;
    if (xQueueSend(channels.ota_network_request,
                   &queued,
                   pdMS_TO_TICKS(100u)) != pdPASS) {
        (void)request_lifecycle_complete(
            &queued->lifecycle, queued->lifecycle.generation, ERR_QUEUE_FULL);
        ota_network_mailbox_busy = 0u;
        return ERR_QUEUE_FULL;
    }
    status = wait_for_ota_io(port, &queued->lifecycle);
    if (queued->lifecycle.state == REQUEST_COMPLETED) {
        ota_network_mailbox_busy = 0u;
        request->length = queued->length;
        request->content_length = queued->content_length;
        if (status == SYS_OK && request->buffer != 0) {
            memcpy(request->buffer, queued->buffer, queued->length);
        }
    }
    return status;
}

static status_t submit_ota_storage_request(ota_rtos_port_t *port,
                                           ota_storage_request_t *request)
{
    ota_storage_request_t *queued = &ota_storage_mailbox;
    status_t status;
    if (ota_storage_mailbox_busy != 0u &&
        queued->lifecycle.state != REQUEST_COMPLETED) {
        return ERR_DEVICE_NOT_READY;
    }
    if (request->data != 0 && request->length > sizeof(ota_storage_io_buffer)) {
        return ERR_INVALID_ARG;
    }
    {
        request_lifecycle_t lifecycle = queued->lifecycle;
        *queued = *request;
        queued->lifecycle = lifecycle;
    }
    if (request->data != 0) {
        memcpy(ota_storage_io_buffer, request->data, request->length);
        queued->data = ota_storage_io_buffer;
    }
    queued->waiter = xTaskGetCurrentTaskHandle();
    queued->status = ERR_DEVICE_NOT_READY;
    (void)request_lifecycle_submit(
        &queued->lifecycle,
        (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
        OTA_STORAGE_IO_TIMEOUT_MS);
    clear_ota_notification();
    ota_storage_mailbox_busy = 1u;
    if (xQueueSend(channels.ota_storage_request,
                   &queued,
                   pdMS_TO_TICKS(100u)) != pdPASS) {
        (void)request_lifecycle_complete(
            &queued->lifecycle, queued->lifecycle.generation, ERR_QUEUE_FULL);
        ota_storage_mailbox_busy = 0u;
        return ERR_QUEUE_FULL;
    }
    status = wait_for_ota_io(port, &queued->lifecycle);
    if (queued->lifecycle.state == REQUEST_COMPLETED) {
        ota_storage_mailbox_busy = 0u;
        request->complete = queued->complete;
    }
    return status;
}

status_t ota_port_fetch_manifest(void *opaque,
                                 const char *url,
                                 uint8_t *buffer,
                                 size_t capacity,
                                 size_t *out_length)
{
    ota_rtos_port_t *port = opaque;
    ota_network_request_t request;
    status_t status;

    memset(&request, 0, sizeof(request));
    request.operation = OTA_NETWORK_FETCH_MANIFEST;
    request.url = url;
    request.buffer = buffer;
    request.capacity = capacity;
    status = submit_ota_network_request(port, &request);
    if (out_length != 0) {
        *out_length = status == SYS_OK ? request.length : 0u;
    }
    return status;
}

status_t
ota_port_http_open(void *opaque, const char *url, uint32_t *out_content_length)
{
    ota_rtos_port_t *port = opaque;
    ota_network_request_t request;
    status_t status;

    memset(&request, 0, sizeof(request));
    request.operation = OTA_NETWORK_OPEN_PACKAGE;
    request.url = url;
    status = submit_ota_network_request(port, &request);
    if (out_content_length != 0) {
        *out_content_length = status == SYS_OK ? request.content_length : 0u;
    }
    return status;
}

status_t ota_port_http_read(void *opaque,
                            uint8_t *buffer,
                            size_t capacity,
                            size_t *out_length)
{
    ota_rtos_port_t *port = opaque;
    ota_network_request_t request;
    status_t status;

    memset(&request, 0, sizeof(request));
    request.operation = OTA_NETWORK_READ_PACKAGE;
    request.buffer = buffer;
    request.capacity = capacity;
    status = submit_ota_network_request(port, &request);
    if (out_length != 0) {
        *out_length = status == SYS_OK ? request.length : 0u;
    }
    return status;
}

status_t ota_port_http_close(void *opaque)
{
    ota_rtos_port_t *port = opaque;
    ota_network_request_t request;

    memset(&request, 0, sizeof(request));
    request.operation = OTA_NETWORK_CLOSE_PACKAGE;
    return submit_ota_network_request(port, &request);
}

status_t ota_port_staging_begin(void *opaque, size_t package_size)
{
    ota_rtos_port_t *port = opaque;
    ota_storage_request_t request;
    status_t status;

    memset(&request, 0, sizeof(request));
    request.operation = OTA_STORAGE_BEGIN;
    request.length = package_size;
    status = submit_ota_storage_request(port, &request);
    while (status == SYS_OK && request.complete == 0u) {
        memset(&request, 0, sizeof(request));
        request.operation = OTA_STORAGE_ERASE_NEXT;
        status = submit_ota_storage_request(port, &request);
    }
    return status;
}

status_t ota_port_staging_write(void *opaque,
                                uint32_t offset,
                                const uint8_t *data,
                                size_t length)
{
    ota_rtos_port_t *port = opaque;
    ota_storage_request_t request;

    memset(&request, 0, sizeof(request));
    request.operation = OTA_STORAGE_WRITE;
    request.offset = offset;
    request.data = data;
    request.length = length;
    return submit_ota_storage_request(port, &request);
}

status_t ota_port_metadata_load(void *opaque,
                                boot_metadata_t *out_metadata,
                                app_slot_t *out_copy_slot)
{
    ota_rtos_port_t *port = opaque;

    return boot_meta_load(
        &port->context->boot_confirmation->store, out_metadata, out_copy_slot);
}

status_t ota_port_metadata_commit(void *opaque,
                                  const boot_meta_commit_request_t *parameters)
{
    if (parameters == 0) {
        return ERR_INVALID_ARG;
    }
    const boot_metadata_t *current = parameters->current;
    app_slot_t current_copy_slot = parameters->current_copy_slot;
    const boot_metadata_t *desired = parameters->desired;
    boot_metadata_t *out_committed = parameters->out_committed;
    app_slot_t *out_copy_slot = parameters->out_copy_slot;

    ota_rtos_port_t *port = opaque;
    status_t status;

    status = power_lock_acquire(PM_LOCK_FLASH_WRITE);
    if (status != SYS_OK) {
        return status;
    }
    status = boot_meta_commit(
        &port->context->boot_confirmation->store,
        &(const boot_meta_commit_request_t){
            current, current_copy_slot, desired, out_committed, out_copy_slot});
    (void)power_lock_release(PM_LOCK_FLASH_WRITE);
    return status;
}

status_t ota_port_staging_metadata_commit(void *opaque,
                                          const uint8_t *record,
                                          size_t record_size)
{
    ota_rtos_port_t *port = opaque;
    ota_storage_request_t request;

    memset(&request, 0, sizeof(request));
    request.operation = OTA_STORAGE_COMMIT_METADATA;
    request.data = record;
    request.length = record_size;
    return submit_ota_storage_request(port, &request);
}

void ota_port_enter_critical(void *opaque)
{
    (void)opaque;

    app_critical_enter();
}

void ota_port_exit_critical(void *opaque)
{
    (void)opaque;

    app_critical_exit();
}
