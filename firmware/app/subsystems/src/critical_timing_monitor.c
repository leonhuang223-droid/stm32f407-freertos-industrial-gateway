#include "critical_timing_monitor.h"

#include <limits.h>

status_t critical_timing_monitor_enter(
    critical_timing_monitor_t *monitor, uint32_t counter)
{
    if (monitor == 0) {
        return ERR_INVALID_ARG;
    }
    if (monitor->nesting == UINT16_MAX) {
        monitor->pairing_errors++;
        return ERR_NO_MEMORY;
    }
    if (monitor->nesting == 0u) {
        monitor->outer_enter_counter = counter;
    }
    monitor->nesting++;
    monitor->entries++;
    if (monitor->nesting > monitor->maximum_nesting) {
        monitor->maximum_nesting = monitor->nesting;
    }
    return SYS_OK;
}

status_t critical_timing_monitor_exit(
    critical_timing_monitor_t *monitor, uint32_t counter)
{
    uint32_t elapsed;

    if (monitor == 0) {
        return ERR_INVALID_ARG;
    }
    if (monitor->nesting == 0u) {
        monitor->pairing_errors++;
        return ERR_INVALID_ARG;
    }
    monitor->nesting--;
    monitor->exits++;
    if (monitor->nesting != 0u) {
        return SYS_OK;
    }
    elapsed = counter - monitor->outer_enter_counter;
    monitor->last_cycles = elapsed;
    if (elapsed > monitor->longest_cycles) {
        monitor->longest_cycles = elapsed;
    }
    return SYS_OK;
}
