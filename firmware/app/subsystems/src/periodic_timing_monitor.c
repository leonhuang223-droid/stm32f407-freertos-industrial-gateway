#include "periodic_timing_monitor.h"

#include <limits.h>
#include <string.h>

status_t periodic_timing_monitor_construct(periodic_timing_monitor_t *monitor,
                                           uint32_t expected_period_ms,
                                           uint32_t release_tolerance_ms)
{
    if (monitor == 0 || expected_period_ms == 0u ||
        expected_period_ms > (uint32_t)INT32_MAX ||
        release_tolerance_ms > (uint32_t)INT32_MAX) {
        return ERR_INVALID_ARG;
    }
    memset(monitor, 0, sizeof(*monitor));
    monitor->stats.expected_period_ms = expected_period_ms;
    monitor->stats.release_tolerance_ms = release_tolerance_ms;
    monitor->initialized = 1u;
    return SYS_OK;
}

status_t periodic_timing_monitor_note(periodic_timing_monitor_t *monitor,
                                      uint32_t actual_release_ms,
                                      uint32_t scheduled_release_ms)
{
    int32_t release_lateness;

    if (monitor == 0 || monitor->initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    release_lateness = (int32_t)(actual_release_ms - scheduled_release_ms);
    monitor->stats.last_release_lateness_ms = release_lateness;
    monitor->stats.releases++;
    if (release_lateness > (int32_t)monitor->stats.release_tolerance_ms) {
        monitor->stats.deadline_misses++;
    }

    if (monitor->previous_release_valid != 0u) {
        uint32_t interval = actual_release_ms - monitor->previous_release_ms;
        int32_t jitter =
            (int32_t)(interval - monitor->stats.expected_period_ms);

        monitor->stats.last_interval_ms = interval;
        monitor->stats.last_jitter_ms = jitter;
        if (monitor->stats.intervals == 0u ||
            interval < monitor->stats.min_interval_ms) {
            monitor->stats.min_interval_ms = interval;
        }
        if (interval > monitor->stats.max_interval_ms) {
            monitor->stats.max_interval_ms = interval;
        }
        if (jitter < 0) {
            uint32_t early = 0u - (uint32_t)jitter;

            if (early > monitor->stats.max_early_ms) {
                monitor->stats.max_early_ms = early;
            }
        } else if ((uint32_t)jitter > monitor->stats.max_late_ms) {
            monitor->stats.max_late_ms = (uint32_t)jitter;
        }
        monitor->stats.intervals++;
    }
    monitor->previous_release_ms = actual_release_ms;
    monitor->previous_release_valid = 1u;
    return SYS_OK;
}

status_t periodic_timing_monitor_get(const periodic_timing_monitor_t *monitor,
                                     periodic_timing_stats_t *stats)
{
    if (monitor == 0 || stats == 0 || monitor->initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    *stats = monitor->stats;
    return SYS_OK;
}
