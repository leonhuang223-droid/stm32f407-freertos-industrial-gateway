#ifndef GATEWAY_LCD_CONTROLLER_H
#define GATEWAY_LCD_CONTROLLER_H

#include "lcd_bus.h"

#include <stddef.h>
#include <stdint.h>

#define LCD_CONTROLLER_ID_ILI9806G 0x9806u
#define LCD_CONTROLLER_ID_NT35510 0x5510u

typedef struct lcd_controller lcd_controller_t;

typedef struct {
    const char *name;
    status_t (*probe)(lcd_bus_t *bus, uint16_t *controller_id);
    status_t (*init)(lcd_controller_t *controller);
    status_t (*set_window)(lcd_controller_t *controller,
                           uint16_t x1, uint16_t y1,
                           uint16_t x2, uint16_t y2);
    status_t (*suspend)(lcd_controller_t *controller);
    status_t (*resume)(lcd_controller_t *controller);
} lcd_controller_ops_t;

struct lcd_controller {
    lcd_bus_t *bus;
    const lcd_controller_ops_t *ops;
    uint16_t controller_id;
    uint16_t width;
    uint16_t height;
    uint8_t initialized;
    uint8_t suspended;
};

status_t lcd_controller_detect(lcd_controller_t *controller, lcd_bus_t *bus,
                               uint16_t width, uint16_t height);
status_t lcd_controller_init(lcd_controller_t *controller);
status_t lcd_controller_flush(lcd_controller_t *controller,
                              uint16_t x1, uint16_t y1,
                              uint16_t x2, uint16_t y2,
                              const uint16_t *pixels, size_t pixel_count);
status_t lcd_controller_suspend(lcd_controller_t *controller);
status_t lcd_controller_resume(lcd_controller_t *controller);
const char *lcd_controller_name(const lcd_controller_t *controller);

const lcd_controller_ops_t *lcd_ili9806g_ops(void);
const lcd_controller_ops_t *lcd_nt35510_ops(void);

#endif
