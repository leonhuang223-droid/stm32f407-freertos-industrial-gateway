#ifndef GATEWAY_SUPERVISOR_SUBSYSTEM_H
#define GATEWAY_SUPERVISOR_SUBSYSTEM_H

#include "error_code.h"
#include "watchdog_device.h"

#include <stdint.h>

#define SUPERVISOR_MAX_TASKS 16u

typedef struct {
    uint8_t task_count;
    uint32_t critical_task_mask;
    uint32_t task_timeout_ms[SUPERVISOR_MAX_TASKS];
    uint32_t startup_grace_ms;
    uint32_t boot_confirm_stable_ms;
} supervisor_config_t;

typedef struct {
    uint32_t checks;
    uint32_t healthy_checks;
    uint32_t unhealthy_checks;
    uint32_t watchdog_refreshes;
    uint32_t watchdog_failures;
    uint32_t stale_task_mask;
    uint32_t seen_task_mask;
    uint32_t latched_faults;
    status_t last_error;
    uint8_t healthy;
} supervisor_health_t;

typedef struct {
    supervisor_config_t config;
    supervisor_health_t health;
    watchdog_device_t *watchdog;
    uint32_t previous_heartbeat[SUPERVISOR_MAX_TASKS];
    uint32_t last_progress_ms[SUPERVISOR_MAX_TASKS];
    uint32_t started_ms;
    uint32_t healthy_since_ms;
    uint8_t initialized;
    uint8_t started;
    uint8_t stable_window_active;
} supervisor_subsystem_t;

status_t supervisor_subsystem_construct(supervisor_subsystem_t *supervisor,
                                        watchdog_device_t *watchdog,
                                        const supervisor_config_t *config);
status_t supervisor_subsystem_start(supervisor_subsystem_t *supervisor,
                                    uint32_t now_ms,
                                    const uint32_t *heartbeat);
status_t supervisor_subsystem_process(supervisor_subsystem_t *supervisor,
                                      uint32_t now_ms,
                                      const uint32_t *heartbeat,
                                      uint8_t services_ready);
void supervisor_subsystem_latch_fault(supervisor_subsystem_t *supervisor,
                                      status_t error);
uint8_t supervisor_subsystem_boot_confirm_ready(
    const supervisor_subsystem_t *supervisor, uint32_t now_ms);
status_t
supervisor_subsystem_get_health(const supervisor_subsystem_t *supervisor,
                                supervisor_health_t *health);

#endif
