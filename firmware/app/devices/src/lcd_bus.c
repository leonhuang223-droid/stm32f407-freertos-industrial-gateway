#include "lcd_bus.h"

#include <string.h>

status_t lcd_bus_construct(lcd_bus_t *bus, const lcd_bus_ops_t *ops,
                           void *context)
{
    if (bus == 0 || ops == 0 || ops->write_command == 0 ||
        ops->write_data == 0 || ops->read_data == 0 ||
        ops->write_pixels == 0) {
        return ERR_INVALID_ARG;
    }
    memset(bus, 0, sizeof(*bus));
    bus->ops = ops;
    bus->context = context;
    bus->initialized = 1u;
    return SYS_OK;
}

status_t lcd_bus_write_command(lcd_bus_t *bus, uint16_t command)
{
    if (bus == 0 || bus->initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    return bus->ops->write_command(bus->context, command);
}

status_t lcd_bus_write_data(lcd_bus_t *bus, uint16_t data)
{
    if (bus == 0 || bus->initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    return bus->ops->write_data(bus->context, data);
}

status_t lcd_bus_read_register(lcd_bus_t *bus, uint16_t command,
                               uint16_t *data, size_t word_count)
{
    size_t i;
    status_t status;

    if (bus == 0 || data == 0 || word_count == 0u ||
        bus->initialized == 0u) {
        return ERR_INVALID_ARG;
    }
    status = lcd_bus_write_command(bus, command);
    for (i = 0u; status == SYS_OK && i < word_count; ++i) {
        status = bus->ops->read_data(bus->context, &data[i]);
    }
    return status;
}

status_t lcd_bus_write_pixels(lcd_bus_t *bus, const uint16_t *pixels,
                              size_t pixel_count)
{
    if (bus == 0 || pixels == 0 || pixel_count == 0u ||
        bus->initialized == 0u) {
        return ERR_INVALID_ARG;
    }
    return bus->ops->write_pixels(bus->context, pixels, pixel_count);
}

void lcd_bus_reset(lcd_bus_t *bus, uint8_t asserted)
{
    if (bus != 0 && bus->initialized != 0u && bus->ops->set_reset != 0) {
        bus->ops->set_reset(bus->context, asserted);
    }
}

void lcd_bus_delay(lcd_bus_t *bus, uint32_t delay_ms)
{
    if (bus != 0 && bus->initialized != 0u && bus->ops->delay_ms != 0) {
        bus->ops->delay_ms(bus->context, delay_ms);
    }
}
