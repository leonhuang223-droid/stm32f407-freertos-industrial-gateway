#include "acquisition_subsystem.h"

#include <string.h>

static void remember_first_error(status_t status, status_t *first_error)
{
    if (status != SYS_OK && *first_error == SYS_OK) {
        *first_error = status;
    }
}

static void refresh_device_health(acquisition_subsystem_t *subsystem)
{
    (void)ads1115_get_health(subsystem->ads1115,
                            &subsystem->health.ads1115);
    (void)max31865_get_health(subsystem->max31865,
                             &subsystem->health.max31865);
    (void)sht30_get_health(subsystem->sht30,
                          &subsystem->health.sht30);
}

static int sample_is_due(uint32_t now_ms, uint32_t next_due_ms)
{
    return (int32_t)(now_ms - next_due_ms) >= 0;
}

static gateway_quality_t quality_from_error(status_t status)
{
    return status == ERR_SENSOR_FAULT
        ? GATEWAY_QUALITY_SENSOR_FAULT
        : GATEWAY_QUALITY_COMM_ERROR;
}

status_t acquisition_subsystem_construct(acquisition_subsystem_t *subsystem,
                                         ads1115_t *ads1115,
                                         max31865_t *max31865,
                                         sht30_t *sht30,
                                         const acquisition_schedule_t *schedule)
{
    if (subsystem == 0 || ads1115 == 0 || max31865 == 0 || sht30 == 0 ||
        schedule == 0 || ads1115->ops == 0 || max31865->ops == 0 ||
        sht30->ops == 0 || schedule->ads1115_period_ms == 0u ||
        schedule->max31865_period_ms == 0u ||
        schedule->sht30_period_ms == 0u) {
        return ERR_INVALID_ARG;
    }
    memset(subsystem, 0, sizeof(*subsystem));
    subsystem->ads1115 = ads1115;
    subsystem->max31865 = max31865;
    subsystem->sht30 = sht30;
    subsystem->schedule = *schedule;
    subsystem->next_sequence = 1u;
    return SYS_OK;
}

status_t acquisition_subsystem_start(acquisition_subsystem_t *subsystem)
{
    status_t first_error = SYS_OK;
    status_t status;

    if (subsystem == 0 || subsystem->ads1115 == 0 ||
        subsystem->max31865 == 0 || subsystem->sht30 == 0) {
        return ERR_INVALID_ARG;
    }

    status = ads1115_init(subsystem->ads1115);
    remember_first_error(status, &first_error);
    status = max31865_init(subsystem->max31865);
    remember_first_error(status, &first_error);
    status = sht30_init(subsystem->sht30);
    remember_first_error(status, &first_error);

    refresh_device_health(subsystem);
    subsystem->health.last_cycle_error = first_error;
    subsystem->initialized = 1u;
    subsystem->suspended = 0u;
    return first_error;
}

status_t acquisition_subsystem_process(
    acquisition_subsystem_t *subsystem, uint32_t now_ms,
    gateway_measurement_t *out_measurements, size_t capacity,
    size_t *out_count)
{
    status_t first_error = SYS_OK;
    status_t status;
    size_t sht_count = 0u;
    size_t i;

    if (subsystem == 0 || out_measurements == 0 || out_count == 0 ||
        capacity < ACQUISITION_MEASUREMENT_COUNT) {
        return ERR_INVALID_ARG;
    }
    *out_count = 0u;
    if (subsystem->initialized == 0u || subsystem->suspended != 0u) {
        return ERR_DEVICE_NOT_READY;
    }

    if (sample_is_due(now_ms, subsystem->ads1115_next_due_ms)) {
        status_t init_status = SYS_OK;

        if (subsystem->ads1115->health.initialized == 0u) {
            init_status = ads1115_init(subsystem->ads1115);
        }
        status = ads1115_sample(subsystem->ads1115, now_ms,
                                &out_measurements[*out_count]);
        if (init_status != SYS_OK) {
            out_measurements[*out_count].error = init_status;
            out_measurements[*out_count].quality =
                quality_from_error(init_status);
            status = init_status;
        }
        (*out_count)++;
        subsystem->ads1115_next_due_ms =
            now_ms + subsystem->schedule.ads1115_period_ms;
        remember_first_error(status, &first_error);
    }
    if (sample_is_due(now_ms, subsystem->max31865_next_due_ms)) {
        status_t init_status = SYS_OK;

        if (subsystem->max31865->health.initialized == 0u) {
            init_status = max31865_init(subsystem->max31865);
        }
        status = max31865_sample(subsystem->max31865, now_ms,
                                 &out_measurements[*out_count]);
        if (init_status != SYS_OK) {
            out_measurements[*out_count].error = init_status;
            out_measurements[*out_count].quality =
                quality_from_error(init_status);
            status = init_status;
        }
        (*out_count)++;
        subsystem->max31865_next_due_ms =
            now_ms + subsystem->schedule.max31865_period_ms;
        remember_first_error(status, &first_error);
    }
    if (sample_is_due(now_ms, subsystem->sht30_next_due_ms)) {
        status_t init_status = SYS_OK;
        size_t first_sht = *out_count;
        size_t j;

        if (subsystem->sht30->health.initialized == 0u) {
            init_status = sht30_init(subsystem->sht30);
        }
        status = sht30_sample(subsystem->sht30, now_ms,
                              &out_measurements[first_sht],
                              capacity - first_sht, &sht_count);
        if (init_status != SYS_OK) {
            for (j = 0u; j < sht_count; ++j) {
                out_measurements[first_sht + j].error = init_status;
                out_measurements[first_sht + j].quality =
                    quality_from_error(init_status);
            }
            status = init_status;
        }
        *out_count += sht_count;
        subsystem->sht30_next_due_ms =
            now_ms + subsystem->schedule.sht30_period_ms;
        remember_first_error(status, &first_error);
    }

    for (i = 0u; i < *out_count; ++i) {
        out_measurements[i].sequence = subsystem->next_sequence++;
    }
    subsystem->health.cycles++;
    if (first_error != SYS_OK) {
        subsystem->health.degraded_cycles++;
    }
    subsystem->health.last_cycle_error = first_error;
    refresh_device_health(subsystem);
    return first_error;
}

status_t acquisition_subsystem_suspend(acquisition_subsystem_t *subsystem)
{
    status_t first_error = SYS_OK;

    if (subsystem == 0 || subsystem->initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if (subsystem->ads1115->health.initialized != 0u) {
        remember_first_error(ads1115_suspend(subsystem->ads1115),
                             &first_error);
    }
    if (subsystem->max31865->health.initialized != 0u) {
        remember_first_error(max31865_suspend(subsystem->max31865),
                             &first_error);
    }
    if (subsystem->sht30->health.initialized != 0u) {
        remember_first_error(sht30_suspend(subsystem->sht30),
                             &first_error);
    }
    subsystem->suspended = first_error == SYS_OK ? 1u : 0u;
    refresh_device_health(subsystem);
    return first_error;
}

status_t acquisition_subsystem_resume(acquisition_subsystem_t *subsystem)
{
    status_t first_error = SYS_OK;

    if (subsystem == 0 || subsystem->initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if (subsystem->ads1115->health.initialized != 0u) {
        remember_first_error(ads1115_resume(subsystem->ads1115),
                             &first_error);
    }
    if (subsystem->max31865->health.initialized != 0u) {
        remember_first_error(max31865_resume(subsystem->max31865),
                             &first_error);
    }
    if (subsystem->sht30->health.initialized != 0u) {
        remember_first_error(sht30_resume(subsystem->sht30),
                             &first_error);
    }
    subsystem->suspended = first_error == SYS_OK ? 0u : 1u;
    refresh_device_health(subsystem);
    return first_error;
}

status_t acquisition_subsystem_get_health(
    const acquisition_subsystem_t *subsystem,
    acquisition_health_t *out_health)
{
    if (subsystem == 0 || out_health == 0) {
        return ERR_INVALID_ARG;
    }
    *out_health = subsystem->health;
    return SYS_OK;
}
