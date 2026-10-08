#include "input_device.h"

#include <limits.h>
#include <string.h>

status_t input_device_construct(input_device_t *input,
                                const input_device_ops_t *ops,
                                void *context,
                                uint16_t width,
                                uint16_t height)
{
    if (input == 0 || ops == 0 || context == 0 || width == 0u || height == 0u ||
        width > INT16_MAX || height > INT16_MAX || ops->init == 0 ||
        ops->read == 0) {
        return ERR_INVALID_ARG;
    }
    memset(input, 0, sizeof(*input));
    input->ops = ops;
    input->context = context;
    input->width = width;
    input->height = height;
    input->health.last_error = ERR_DEVICE_NOT_READY;
    return SYS_OK;
}

status_t input_device_init(input_device_t *input)
{
    status_t status;

    if (input == 0 || input->ops == 0) {
        return ERR_INVALID_ARG;
    }
    status = input->ops->init(input->context, input->width, input->height);
    input->health.last_error = status;
    input->health.initialized = status == SYS_OK ? 1u : 0u;
    input->health.suspended = 0u;
    return status;
}

status_t input_device_read(input_device_t *input, input_sample_t *sample)
{
    status_t status;

    if (input == 0 || sample == 0 || input->health.initialized == 0u ||
        input->health.suspended != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = input->ops->read(input->context, sample);
    input->health.last_error = status;
    if (status == SYS_OK) {
        if (sample->x < 0 || sample->y < 0 ||
            sample->x >= (int16_t)input->width ||
            sample->y >= (int16_t)input->height ||
            (uint32_t)sample->state > INPUT_STATE_PRESSED) {
            input->health.read_errors++;
            input->health.last_error = ERR_INVALID_ARG;
            return ERR_INVALID_ARG;
        }
        input->health.samples++;
    } else {
        input->health.read_errors++;
    }
    return status;
}

status_t input_device_suspend(input_device_t *input)
{
    status_t status;

    if (input == 0 || input->health.initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status =
        input->ops->suspend != 0 ? input->ops->suspend(input->context) : SYS_OK;
    input->health.last_error = status;
    if (status == SYS_OK) {
        input->health.suspended = 1u;
    }
    return status;
}

status_t input_device_resume(input_device_t *input)
{
    status_t status;

    if (input == 0 || input->ops == 0) {
        return ERR_INVALID_ARG;
    }
    status =
        input->ops->resume != 0
            ? input->ops->resume(input->context)
            : input->ops->init(input->context, input->width, input->height);
    input->health.last_error = status;
    if (status == SYS_OK) {
        input->health.initialized = 1u;
        input->health.suspended = 0u;
    }
    return status;
}

status_t input_device_get_health(const input_device_t *input,
                                 input_device_health_t *health)
{
    if (input == 0 || health == 0) {
        return ERR_INVALID_ARG;
    }
    *health = input->health;
    return SYS_OK;
}
