#ifndef GATEWAY_DISPLAY_DEVICE_H
#define GATEWAY_DISPLAY_DEVICE_H

#include "error_code.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
} display_area_t;

typedef struct {
    status_t (*init)(void *context, uint16_t width, uint16_t height);
    status_t (*flush)(void *context, const display_area_t *area,
                      const uint16_t *pixels, size_t pixel_count);
    status_t (*set_backlight)(void *context, uint8_t percent);
    status_t (*suspend)(void *context);
    status_t (*resume)(void *context);
} display_device_ops_t;

typedef struct {
    uint32_t flush_count;
    uint32_t flush_errors;
    uint32_t pixels_flushed;
    status_t last_error;
    uint8_t initialized;
    uint8_t suspended;
    uint8_t backlight_percent;
} display_device_health_t;

typedef struct {
    const display_device_ops_t *ops;
    void *context;
    uint16_t width;
    uint16_t height;
    display_device_health_t health;
} display_device_t;

status_t display_device_construct(display_device_t *display,
                                  const display_device_ops_t *ops,
                                  void *context, uint16_t width,
                                  uint16_t height);
status_t display_device_init(display_device_t *display);
status_t display_device_flush(display_device_t *display,
                              const display_area_t *area,
                              const uint16_t *pixels, size_t pixel_count);
status_t display_device_set_backlight(display_device_t *display,
                                      uint8_t percent);
status_t display_device_suspend(display_device_t *display);
status_t display_device_resume(display_device_t *display);
status_t display_device_get_health(const display_device_t *display,
                                   display_device_health_t *health);

#endif
