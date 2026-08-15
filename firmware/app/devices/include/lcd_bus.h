#ifndef GATEWAY_LCD_BUS_H
#define GATEWAY_LCD_BUS_H

#include "error_code.h"

#include <stddef.h>
#include <stdint.h>

typedef struct lcd_bus lcd_bus_t;

typedef struct {
    status_t (*write_command)(void *context, uint16_t command);
    status_t (*write_data)(void *context, uint16_t data);
    status_t (*read_data)(void *context, uint16_t *data);
    status_t (*write_pixels)(void *context, const uint16_t *pixels,
                             size_t pixel_count);
    void (*set_reset)(void *context, uint8_t asserted);
    void (*delay_ms)(void *context, uint32_t delay_ms);
} lcd_bus_ops_t;

struct lcd_bus {
    const lcd_bus_ops_t *ops;
    void *context;
    uint8_t initialized;
};

status_t lcd_bus_construct(lcd_bus_t *bus, const lcd_bus_ops_t *ops,
                           void *context);
status_t lcd_bus_write_command(lcd_bus_t *bus, uint16_t command);
status_t lcd_bus_write_data(lcd_bus_t *bus, uint16_t data);
status_t lcd_bus_read_register(lcd_bus_t *bus, uint16_t command,
                               uint16_t *data, size_t word_count);
status_t lcd_bus_write_pixels(lcd_bus_t *bus, const uint16_t *pixels,
                              size_t pixel_count);
void lcd_bus_reset(lcd_bus_t *bus, uint8_t asserted);
void lcd_bus_delay(lcd_bus_t *bus, uint32_t delay_ms);

#endif
