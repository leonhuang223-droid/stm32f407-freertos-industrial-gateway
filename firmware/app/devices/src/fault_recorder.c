#include "fault_recorder.h"

#include "crc32.h"

#include <stddef.h>
#include <string.h>

static status_t compute_crc(const fault_record_t *record, uint32_t *crc)
{
    return crc32_compute((const uint8_t *)record,
                         offsetof(fault_record_t, crc32), crc);
}

status_t fault_record_finalize(fault_record_t *record)
{
    if (record == 0 || record->origin <= FAULT_ORIGIN_NONE ||
        record->origin >= FAULT_ORIGIN_COUNT) {
        return ERR_INVALID_ARG;
    }
    record->magic = FAULT_RECORD_MAGIC;
    record->version = FAULT_RECORD_VERSION;
    record->reserved = 0u;
    record->crc32 = 0u;
    return compute_crc(record, &record->crc32);
}

status_t fault_record_validate(const fault_record_t *record)
{
    uint32_t crc;
    status_t status;

    if (record == 0) {
        return ERR_INVALID_ARG;
    }
    if (record->magic != FAULT_RECORD_MAGIC ||
        record->version != FAULT_RECORD_VERSION ||
        record->origin <= FAULT_ORIGIN_NONE ||
        record->origin >= FAULT_ORIGIN_COUNT) {
        return ERR_METADATA_INVALID;
    }
    status = compute_crc(record, &crc);
    if (status != SYS_OK) {
        return status;
    }
    return crc == record->crc32 ? SYS_OK : ERR_CRC;
}

const char *fault_origin_name(fault_origin_t origin)
{
    static const char *const names[FAULT_ORIGIN_COUNT] = {
        "none", "hardfault", "memmanage", "busfault", "usagefault",
        "watchdog-injection"
    };

    return (unsigned int)origin < FAULT_ORIGIN_COUNT
        ? names[(unsigned int)origin] : "unknown";
}

status_t fault_recorder_refresh(fault_recorder_t *recorder)
{
    fault_record_t record;
    status_t status;

    if (recorder == 0 || recorder->initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    memset(&record, 0, sizeof(record));
    recorder->health.load_attempts++;
    status = recorder->ops->load(recorder->context, &record);
    if (status == SYS_OK) {
        status = fault_record_validate(&record);
    }
    if (status == SYS_OK) {
        recorder->latest = record;
        recorder->health.record_valid = 1u;
        recorder->health.valid_records++;
    } else {
        memset(&recorder->latest, 0, sizeof(recorder->latest));
        recorder->health.record_valid = 0u;
        if (status != ERR_DEVICE_NOT_READY) {
            recorder->health.invalid_records++;
        }
    }
    recorder->health.last_error = status == ERR_DEVICE_NOT_READY
        ? SYS_OK : status;
    return status;
}

status_t fault_recorder_construct(fault_recorder_t *recorder,
                                  const fault_recorder_ops_t *ops,
                                  void *context,
                                  uint8_t injection_enabled)
{
    if (recorder == 0 || ops == 0 || ops->load == 0 || ops->clear == 0) {
        return ERR_INVALID_ARG;
    }
    memset(recorder, 0, sizeof(*recorder));
    recorder->ops = ops;
    recorder->context = context;
    recorder->health.injection_enabled = injection_enabled != 0u ? 1u : 0u;
    recorder->health.last_error = SYS_OK;
    recorder->initialized = 1u;
    (void)fault_recorder_refresh(recorder);
    return SYS_OK;
}

status_t fault_recorder_get(const fault_recorder_t *recorder,
                            fault_record_t *record)
{
    if (recorder == 0 || recorder->initialized == 0u || record == 0) {
        return ERR_INVALID_ARG;
    }
    if (recorder->health.record_valid == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    *record = recorder->latest;
    return SYS_OK;
}

status_t fault_recorder_clear(fault_recorder_t *recorder)
{
    status_t status;

    if (recorder == 0 || recorder->initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = recorder->ops->clear(recorder->context);
    if (status == SYS_OK) {
        memset(&recorder->latest, 0, sizeof(recorder->latest));
        recorder->health.record_valid = 0u;
        recorder->health.clear_count++;
    }
    recorder->health.last_error = status;
    return status;
}

status_t fault_recorder_inject(fault_recorder_t *recorder,
                               fault_injection_t injection,
                               uint32_t confirmation)
{
    status_t status;

    if (recorder == 0 || recorder->initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if ((unsigned int)injection >= FAULT_INJECTION_COUNT ||
        confirmation != FAULT_INJECTION_CONFIRMATION) {
        return ERR_INVALID_ARG;
    }
    if (recorder->health.injection_enabled == 0u ||
        recorder->ops->inject == 0) {
        return ERR_UNSUPPORTED;
    }
    status = recorder->ops->inject(recorder->context, injection);
    if (status == SYS_OK || status == ERR_RESET_REQUIRED) {
        recorder->health.injection_count++;
    }
    recorder->health.last_error = status;
    return status;
}

status_t fault_recorder_get_health(const fault_recorder_t *recorder,
                                   fault_recorder_health_t *health)
{
    if (recorder == 0 || recorder->initialized == 0u || health == 0) {
        return ERR_INVALID_ARG;
    }
    *health = recorder->health;
    return SYS_OK;
}
