#include "request_lifecycle.h"
#include <limits.h>

static int expired(const request_lifecycle_t *request, uint32_t now_ms)
{
    return (int32_t)(now_ms - request->deadline_ms) >= 0;
}

status_t request_lifecycle_submit(request_lifecycle_t *request,
                                  uint32_t now_ms,
                                  uint32_t timeout_ms)
{
    if (request == 0 || timeout_ms == 0u || timeout_ms > INT32_MAX) {
        return ERR_INVALID_ARG;
    }
    if (request->state == REQUEST_QUEUED || request->state == REQUEST_RUNNING) {
        return ERR_DEVICE_NOT_READY;
    }
    request->generation++;
    if (request->generation == 0u) {
        request->generation = 1u;
    }
    request->deadline_ms = now_ms + timeout_ms;
    request->cancel_requested = 0u;
    request->result = ERR_IN_PROGRESS;
    request->state = REQUEST_QUEUED;
    return SYS_OK;
}

status_t request_lifecycle_begin(request_lifecycle_t *request,
                                 uint32_t generation,
                                 uint32_t now_ms)
{
    status_t result;
    if (request == 0 || generation != request->generation ||
        request->state != REQUEST_QUEUED) {
        return ERR_INVALID_ARG;
    }
    result = expired(request, now_ms)
                 ? ERR_TIMEOUT
                 : (request->cancel_requested != 0u ? ERR_OTA_ABORTED : SYS_OK);
    if (result != SYS_OK) {
        request->result = result;
        request->state = REQUEST_COMPLETED;
        return result;
    }
    request->state = REQUEST_RUNNING;
    return SYS_OK;
}

status_t request_lifecycle_poll(const request_lifecycle_t *request,
                                uint32_t now_ms)
{
    if (request == 0 || request->state == REQUEST_IDLE) {
        return ERR_INVALID_ARG;
    }
    if (request->state == REQUEST_COMPLETED) {
        return request->result;
    }
    return expired(request, now_ms) ? ERR_TIMEOUT : ERR_IN_PROGRESS;
}

status_t request_lifecycle_complete(request_lifecycle_t *request,
                                    uint32_t generation,
                                    status_t result)
{
    if (request == 0 || generation != request->generation ||
        (request->state != REQUEST_RUNNING &&
         request->state != REQUEST_QUEUED) ||
        result == ERR_IN_PROGRESS) {
        return ERR_INVALID_ARG;
    }
    request->result = result;
    request->state = REQUEST_COMPLETED;
    return SYS_OK;
}

void request_lifecycle_cancel(request_lifecycle_t *request)
{
    if (request != 0 && (request->state == REQUEST_QUEUED ||
                         request->state == REQUEST_RUNNING)) {
        request->cancel_requested = 1u;
    }
}
