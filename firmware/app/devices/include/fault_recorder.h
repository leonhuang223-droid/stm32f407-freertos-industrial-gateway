#ifndef GATEWAY_FAULT_RECORDER_H
#define GATEWAY_FAULT_RECORDER_H

#include "error_code.h"

#include <stdint.h>

#define FAULT_RECORD_MAGIC 0x464C5431u
#define FAULT_RECORD_VERSION 1u
#define FAULT_INJECTION_CONFIRMATION 0x434F4E46u

typedef enum {
    FAULT_ORIGIN_NONE = 0,
    FAULT_ORIGIN_HARDFAULT,
    FAULT_ORIGIN_MEMMANAGE,
    FAULT_ORIGIN_BUSFAULT,
    FAULT_ORIGIN_USAGEFAULT,
    FAULT_ORIGIN_WATCHDOG_INJECTION,
    FAULT_ORIGIN_COUNT
} fault_origin_t;

typedef enum {
    FAULT_INJECTION_HARDFAULT = 0,
    FAULT_INJECTION_WATCHDOG,
    FAULT_INJECTION_COUNT
} fault_injection_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t origin;
    uint8_t reserved;
    uint32_t sequence;
    uint32_t reset_flags;
    uint32_t exception_return;
    uint32_t task_token;
    uint32_t stacked_r0;
    uint32_t stacked_r1;
    uint32_t stacked_r2;
    uint32_t stacked_r3;
    uint32_t stacked_r12;
    uint32_t stacked_lr;
    uint32_t stacked_pc;
    uint32_t stacked_xpsr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t crc32;
} fault_record_t;

typedef struct {
    status_t (*load)(void *context, fault_record_t *record);
    status_t (*clear)(void *context);
    status_t (*inject)(void *context, fault_injection_t injection);
} fault_recorder_ops_t;

typedef struct {
    uint32_t load_attempts;
    uint32_t valid_records;
    uint32_t invalid_records;
    uint32_t clear_count;
    uint32_t injection_count;
    status_t last_error;
    uint8_t record_valid;
    uint8_t injection_enabled;
} fault_recorder_health_t;

typedef struct {
    const fault_recorder_ops_t *ops;
    void *context;
    fault_record_t latest;
    fault_recorder_health_t health;
    uint8_t initialized;
} fault_recorder_t;

status_t fault_record_finalize(fault_record_t *record);
status_t fault_record_validate(const fault_record_t *record);
const char *fault_origin_name(fault_origin_t origin);

status_t fault_recorder_construct(fault_recorder_t *recorder,
                                  const fault_recorder_ops_t *ops,
                                  void *context,
                                  uint8_t injection_enabled);
status_t fault_recorder_refresh(fault_recorder_t *recorder);
status_t fault_recorder_get(const fault_recorder_t *recorder,
                            fault_record_t *record);
status_t fault_recorder_clear(fault_recorder_t *recorder);
status_t fault_recorder_inject(fault_recorder_t *recorder,
                               fault_injection_t injection,
                               uint32_t confirmation);
status_t fault_recorder_get_health(const fault_recorder_t *recorder,
                                   fault_recorder_health_t *health);

#endif
