#ifndef GATEWAY_CRITICAL_TIMING_MONITOR_H
#define GATEWAY_CRITICAL_TIMING_MONITOR_H

#include "error_code.h"

#include <stdint.h>

typedef struct {
    uint32_t entries;
    uint32_t exits;
    uint32_t pairing_errors;
    uint32_t longest_cycles;
    uint32_t last_cycles;
    uint16_t nesting;
    uint16_t maximum_nesting;
    uint32_t outer_enter_counter;
} critical_timing_monitor_t;

status_t critical_timing_monitor_enter(critical_timing_monitor_t *monitor,
                                       uint32_t counter);
status_t critical_timing_monitor_exit(critical_timing_monitor_t *monitor,
                                      uint32_t counter);

#endif
