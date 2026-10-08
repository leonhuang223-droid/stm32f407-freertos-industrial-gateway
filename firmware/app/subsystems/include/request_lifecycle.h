#ifndef GATEWAY_REQUEST_LIFECYCLE_H
#define GATEWAY_REQUEST_LIFECYCLE_H

#include "error_code.h"
#include <stdint.h>

typedef enum {
    REQUEST_IDLE = 0,
    REQUEST_QUEUED,
    REQUEST_RUNNING,
    REQUEST_COMPLETED
} request_state_t;

/* Caller owns submission; worker owns completion. Payload storage must outlive
 * both. RTOS adapters serialize transitions with their normal critical section.
 */
typedef struct {
    uint32_t generation;
    uint32_t deadline_ms;
    status_t result;
    volatile request_state_t state;
    volatile uint8_t cancel_requested;
} request_lifecycle_t;

status_t request_lifecycle_submit(request_lifecycle_t *request,
                                  uint32_t now_ms,
                                  uint32_t timeout_ms);
status_t request_lifecycle_begin(request_lifecycle_t *request,
                                 uint32_t generation,
                                 uint32_t now_ms);
status_t request_lifecycle_poll(const request_lifecycle_t *request,
                                uint32_t now_ms);
status_t request_lifecycle_complete(request_lifecycle_t *request,
                                    uint32_t generation,
                                    status_t result);
void request_lifecycle_cancel(request_lifecycle_t *request);

#endif
