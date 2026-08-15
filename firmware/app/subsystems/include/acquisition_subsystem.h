#ifndef GATEWAY_ACQUISITION_SUBSYSTEM_H
#define GATEWAY_ACQUISITION_SUBSYSTEM_H

#include "ads1115.h"
#include "max31865.h"
#include "sht30.h"

#include <stddef.h>
#include <stdint.h>

#define ACQUISITION_MEASUREMENT_COUNT 4u

typedef struct {
    uint32_t cycles;
    uint32_t degraded_cycles;
    status_t last_cycle_error;
    gateway_device_health_t ads1115;
    gateway_device_health_t max31865;
    gateway_device_health_t sht30;
} acquisition_health_t;

typedef struct {
    uint32_t ads1115_period_ms;
    uint32_t max31865_period_ms;
    uint32_t sht30_period_ms;
} acquisition_schedule_t;

typedef struct {
    ads1115_t *ads1115;
    max31865_t *max31865;
    sht30_t *sht30;
    acquisition_schedule_t schedule;
    uint32_t ads1115_next_due_ms;
    uint32_t max31865_next_due_ms;
    uint32_t sht30_next_due_ms;
    uint32_t next_sequence;
    acquisition_health_t health;
    uint8_t initialized;
    uint8_t suspended;
} acquisition_subsystem_t;

status_t acquisition_subsystem_construct(acquisition_subsystem_t *subsystem,
                                         ads1115_t *ads1115,
                                         max31865_t *max31865,
                                         sht30_t *sht30,
                                         const acquisition_schedule_t *schedule);
status_t acquisition_subsystem_start(acquisition_subsystem_t *subsystem);
status_t acquisition_subsystem_process(
    acquisition_subsystem_t *subsystem, uint32_t now_ms,
    gateway_measurement_t *out_measurements, size_t capacity,
    size_t *out_count);
status_t acquisition_subsystem_suspend(acquisition_subsystem_t *subsystem);
status_t acquisition_subsystem_resume(acquisition_subsystem_t *subsystem);
status_t acquisition_subsystem_get_health(
    const acquisition_subsystem_t *subsystem,
    acquisition_health_t *out_health);

#endif
