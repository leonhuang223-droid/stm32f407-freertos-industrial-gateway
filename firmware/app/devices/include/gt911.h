#ifndef GATEWAY_GT911_H
#define GATEWAY_GT911_H

#include "i2c_bus.h"
#include "input_device.h"

#include <stdint.h>

#define GT911_DEFAULT_ADDRESS 0x5Du
#define GT911_MAX_TOUCHES 5u

typedef struct {
    status_t (*select_address)(void *context, uint8_t address);
    status_t (*wake)(void *context);
} gt911_io_ops_t;

typedef struct {
    uint8_t address;
    uint16_t logical_width;
    uint16_t logical_height;
    uint8_t swap_xy;
    uint8_t invert_x;
    uint8_t invert_y;
} gt911_config_t;

typedef struct {
    i2c_bus_t *bus;
    const gt911_io_ops_t *io_ops;
    void *io_context;
    gt911_config_t config;
    char product_id[5];
    uint16_t panel_width;
    uint16_t panel_height;
    input_sample_t last_sample;
    uint8_t initialized;
    uint8_t suspended;
} gt911_t;

status_t gt911_construct(gt911_t *device,
                         i2c_bus_t *bus,
                         const gt911_io_ops_t *io_ops,
                         void *io_context,
                         const gt911_config_t *config);
status_t gt911_init(gt911_t *device);
status_t gt911_read(gt911_t *device, input_sample_t *sample);
status_t gt911_suspend(gt911_t *device);
status_t gt911_resume(gt911_t *device);

#endif
