#ifndef GATEWAY_STORAGE_SUBSYSTEM_H
#define GATEWAY_STORAGE_SUBSYSTEM_H

#include "external_flash_layout.h"
#include "fault_recorder.h"
#include "gateway_model.h"
#include "storage_media.h"

#include <stdint.h>

#define STORAGE_RECORD_SIZE 128u
#define STORAGE_CONFIG_WIRE_SIZE 256u

typedef struct {
    external_flash_partition_t partition;
    uint32_t active_sector;
    uint32_t generation;
    uint16_t next_slot;
    uint8_t region_id;
} storage_region_cursor_t;

typedef struct {
    uint32_t log_records;
    uint32_t alarm_records;
    uint32_t crash_archives;
    uint32_t crash_duplicates;
    uint32_t latest_crash_sequence;
    uint32_t config_commits;
    uint32_t sector_erases;
    uint32_t mount_invalid_records;
    uint32_t write_failures;
    uint32_t verify_failures;
    uint32_t power_downs;
    uint32_t wakeups;
    uint32_t power_failures;
    uint32_t active_config_generation;
    status_t last_error;
    uint8_t mounted;
    uint8_t active_config_copy;
    uint8_t latest_crash_valid;
    uint8_t powered_down;
} storage_health_t;

typedef struct {
    storage_media_t *media;
    gateway_runtime_config_t default_config;
    gateway_runtime_config_t loaded_config;
    storage_region_cursor_t runtime_log;
    storage_region_cursor_t alarm_log;
    storage_region_cursor_t crash_log;
    fault_record_t latest_fault;
    storage_health_t health;
} storage_subsystem_t;

status_t storage_subsystem_construct(
    storage_subsystem_t *subsystem, storage_media_t *media,
    const gateway_runtime_config_t *default_config);
status_t storage_subsystem_start(storage_subsystem_t *subsystem);
status_t storage_subsystem_append_log(
    storage_subsystem_t *subsystem,
    const gateway_storage_log_request_t *request);
status_t storage_subsystem_append_alarm(
    storage_subsystem_t *subsystem,
    const gateway_storage_alarm_request_t *request);
status_t storage_subsystem_archive_fault(
    storage_subsystem_t *subsystem, const fault_record_t *record);
status_t storage_subsystem_load_latest_fault(
    const storage_subsystem_t *subsystem, fault_record_t *record);
status_t storage_subsystem_save_config(
    storage_subsystem_t *subsystem,
    const gateway_storage_config_request_t *request);
status_t storage_subsystem_load_config(
    const storage_subsystem_t *subsystem,
    gateway_runtime_config_t *config);
status_t storage_subsystem_power_down(storage_subsystem_t *subsystem);
status_t storage_subsystem_wake(storage_subsystem_t *subsystem);
status_t storage_subsystem_get_health(const storage_subsystem_t *subsystem,
                                      storage_health_t *health);

#endif
