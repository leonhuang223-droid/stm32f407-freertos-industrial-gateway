#ifndef GATEWAY_F407_LCD_BUS_H
#define GATEWAY_F407_LCD_BUS_H

#include "lcd_bus.h"

#include <stdint.h>

#define F407_LCD_WIDTH 800u
#define F407_LCD_HEIGHT 480u
#define F407_EXTERNAL_DRAW_BUFFER_PIXELS (F407_LCD_WIDTH * 10u)

status_t f407_lcd_bus_configure(lcd_bus_t *bus);
status_t f407_lcd_backlight_set(uint8_t percent);
status_t f407_external_sram_self_test(void);
uint16_t *f407_external_draw_buffer(unsigned int index);

#endif
