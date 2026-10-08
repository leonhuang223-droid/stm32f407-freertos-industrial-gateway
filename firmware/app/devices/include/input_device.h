#ifndef GATEWAY_INPUT_DEVICE_H
#define GATEWAY_INPUT_DEVICE_H

#include "error_code.h"

#include <stdint.h>

typedef enum { INPUT_STATE_RELEASED = 0, INPUT_STATE_PRESSED } input_state_t;

typedef struct {
    int16_t x;
    int16_t y;
    input_state_t state;
    uint32_t timestamp_ms;
} input_sample_t;

typedef struct {
    status_t (*init)(void *context, uint16_t width, uint16_t height);
    status_t (*read)(void *context, input_sample_t *sample);
    status_t (*suspend)(void *context);
    status_t (*resume)(void *context);
} input_device_ops_t;

typedef struct {
    uint32_t samples;
    uint32_t read_errors;
    status_t last_error;
    uint8_t initialized;
    uint8_t suspended;
} input_device_health_t;

typedef struct {
    const input_device_ops_t *ops;
    void *context;
    uint16_t width;
    uint16_t height;
    input_device_health_t health;
} input_device_t;

status_t input_device_construct(input_device_t *input,
                                const input_device_ops_t *ops,
                                void *context,
                                uint16_t width,
                                uint16_t height);
status_t input_device_init(input_device_t *input);
status_t input_device_read(input_device_t *input, input_sample_t *sample);
status_t input_device_suspend(input_device_t *input);
status_t input_device_resume(input_device_t *input);
status_t input_device_get_health(const input_device_t *input,
                                 input_device_health_t *health);

#endif
