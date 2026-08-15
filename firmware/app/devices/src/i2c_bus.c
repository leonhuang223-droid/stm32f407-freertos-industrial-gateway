#include "i2c_bus.h"

#include <string.h>

status_t i2c_bus_construct(i2c_bus_t *bus, const i2c_bus_ops_t *ops,
                           void *context, uint32_t timeout_ms)
{
    if (bus == 0 || ops == 0 || ops->write == 0 || ops->read == 0 ||
        ops->write_read == 0 || ops->delay_ms == 0 || timeout_ms == 0u) {
        return ERR_INVALID_ARG;
    }
    memset(bus, 0, sizeof(*bus));
    bus->ops = ops;
    bus->context = context;
    bus->timeout_ms = timeout_ms;
    bus->initialized = 1u;
    return SYS_OK;
}

static status_t ready(const i2c_bus_t *bus)
{
    return bus != 0 && bus->initialized != 0u && bus->ops != 0
        ? SYS_OK
        : ERR_DEVICE_NOT_READY;
}

status_t i2c_bus_write(i2c_bus_t *bus, uint8_t address,
                       const uint8_t *data, size_t length)
{
    status_t status = ready(bus);

    if (status != SYS_OK) {
        return status;
    }
    if (address > 0x7Fu || data == 0 || length == 0u) {
        return ERR_INVALID_ARG;
    }
    return bus->ops->write(bus->context, address, data, length);
}

status_t i2c_bus_read(i2c_bus_t *bus, uint8_t address,
                      uint8_t *data, size_t length)
{
    status_t status = ready(bus);

    if (status != SYS_OK) {
        return status;
    }
    if (address > 0x7Fu || data == 0 || length == 0u) {
        return ERR_INVALID_ARG;
    }
    return bus->ops->read(bus->context, address, data, length);
}

status_t i2c_bus_write_read(i2c_bus_t *bus, uint8_t address,
                            const uint8_t *write_data, size_t write_length,
                            uint8_t *read_data, size_t read_length)
{
    status_t status = ready(bus);

    if (status != SYS_OK) {
        return status;
    }
    if (address > 0x7Fu || write_data == 0 || write_length == 0u ||
        read_data == 0 || read_length == 0u) {
        return ERR_INVALID_ARG;
    }
    return bus->ops->write_read(bus->context, address, write_data,
                                write_length, read_data, read_length);
}

void i2c_bus_delay(i2c_bus_t *bus, uint32_t delay_ms)
{
    if (ready(bus) == SYS_OK && delay_ms != 0u) {
        bus->ops->delay_ms(bus->context, delay_ms);
    }
}
