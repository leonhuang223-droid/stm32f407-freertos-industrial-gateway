#ifndef GATEWAY_PERIODIC_TIMING_MONITOR_H
#define GATEWAY_PERIODIC_TIMING_MONITOR_H

#include "error_code.h"

#include <stdint.h>

typedef struct {
    uint32_t expected_period_ms;
    uint32_t release_tolerance_ms;
    uint32_t releases;
    uint32_t intervals;
    uint32_t last_interval_ms;
    uint32_t min_interval_ms;
    uint32_t max_interval_ms;
    int32_t last_jitter_ms;
    int32_t last_release_lateness_ms;
    uint32_t max_early_ms;
    uint32_t max_late_ms;
    uint32_t deadline_misses;
} periodic_timing_stats_t;

typedef struct {
    periodic_timing_stats_t stats;
    uint32_t previous_release_ms;
    uint8_t previous_release_valid;
    uint8_t initialized;
} periodic_timing_monitor_t;

status_t periodic_timing_monitor_construct(periodic_timing_monitor_t *monitor,
                                           uint32_t expected_period_ms,
                                           uint32_t release_tolerance_ms);
status_t periodic_timing_monitor_note(periodic_timing_monitor_t *monitor,
                                      uint32_t actual_release_ms,
                                      uint32_t scheduled_release_ms);
status_t periodic_timing_monitor_get(const periodic_timing_monitor_t *monitor,
                                     periodic_timing_stats_t *stats);

#endif
