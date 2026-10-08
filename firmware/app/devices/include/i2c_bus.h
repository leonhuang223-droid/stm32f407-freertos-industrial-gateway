#ifndef GATEWAY_I2C_BUS_H
#define GATEWAY_I2C_BUS_H

#include "error_code.h"

#include <stddef.h>
#include <stdint.h>

typedef struct i2c_bus i2c_bus_t;

/** Buffers remain valid until the owner task returns. */
typedef struct {
    const uint8_t *write_data;
    size_t write_length;
    uint8_t *read_data;
    size_t read_length;
} i2c_transfer_t;

typedef struct {
    status_t (*write)(void *context,
                      uint8_t address,
                      const uint8_t *data,
                      size_t length);
    status_t (*read)(void *context,
                     uint8_t address,
                     uint8_t *data,
                     size_t length);
    status_t (*write_read)(void *context,
                           uint8_t address,
                           const i2c_transfer_t *parameters);
    void (*delay_ms)(void *context, uint32_t delay_ms);
} i2c_bus_ops_t;

struct i2c_bus {
    const i2c_bus_ops_t *ops;
    void *context;
    uint32_t timeout_ms;
    uint8_t initialized;
};

status_t i2c_bus_construct(i2c_bus_t *bus,
                           const i2c_bus_ops_t *ops,
                           void *context,
                           uint32_t timeout_ms);
status_t i2c_bus_write(i2c_bus_t *bus,
                       uint8_t address,
                       const uint8_t *data,
                       size_t length);
status_t
i2c_bus_read(i2c_bus_t *bus, uint8_t address, uint8_t *data, size_t length);
status_t i2c_bus_write_read(i2c_bus_t *bus,
                            uint8_t address,
                            const i2c_transfer_t *parameters);
void i2c_bus_delay(i2c_bus_t *bus, uint32_t delay_ms);

#endif
