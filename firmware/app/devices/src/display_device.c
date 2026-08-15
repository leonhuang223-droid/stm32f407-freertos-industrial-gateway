#include "display_device.h"

#include <string.h>

status_t display_device_construct(display_device_t *display,
                                  const display_device_ops_t *ops,
                                  void *context, uint16_t width,
                                  uint16_t height)
{
    if (display == 0 || ops == 0 || context == 0 || width == 0u ||
        height == 0u || ops->init == 0 || ops->flush == 0) {
        return ERR_INVALID_ARG;
    }
    memset(display, 0, sizeof(*display));
    display->ops = ops;
    display->context = context;
    display->width = width;
    display->height = height;
    display->health.last_error = ERR_DEVICE_NOT_READY;
    return SYS_OK;
}

status_t display_device_init(display_device_t *display)
{
    status_t status;

    if (display == 0 || display->ops == 0) {
        return ERR_INVALID_ARG;
    }
    status = display->ops->init(display->context, display->width,
                                display->height);
    display->health.last_error = status;
    display->health.initialized = status == SYS_OK ? 1u : 0u;
    display->health.suspended = 0u;
    return status;
}

status_t display_device_flush(display_device_t *display,
                              const display_area_t *area,
                              const uint16_t *pixels, size_t pixel_count)
{
    status_t status;

    if (display == 0 || area == 0 || pixels == 0 || pixel_count == 0u ||
        display->health.initialized == 0u ||
        display->health.suspended != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if (area->x1 < 0 || area->y1 < 0 || area->x2 < area->x1 ||
        area->y2 < area->y1 || area->x2 >= (int16_t)display->width ||
        area->y2 >= (int16_t)display->height) {
        return ERR_INVALID_ARG;
    }
    status = display->ops->flush(display->context, area, pixels,
                                 pixel_count);
    display->health.last_error = status;
    if (status == SYS_OK) {
        display->health.flush_count++;
        display->health.pixels_flushed += (uint32_t)pixel_count;
    } else {
        display->health.flush_errors++;
    }
    return status;
}

status_t display_device_set_backlight(display_device_t *display,
                                      uint8_t percent)
{
    status_t status;

    if (display == 0 || display->health.initialized == 0u ||
        percent > 100u) {
        return ERR_INVALID_ARG;
    }
    status = display->ops->set_backlight != 0
        ? display->ops->set_backlight(display->context, percent) : SYS_OK;
    display->health.last_error = status;
    if (status == SYS_OK) {
        display->health.backlight_percent = percent;
    }
    return status;
}

status_t display_device_suspend(display_device_t *display)
{
    status_t status;

    if (display == 0 || display->health.initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = display->ops->suspend != 0
        ? display->ops->suspend(display->context) : SYS_OK;
    display->health.last_error = status;
    if (status == SYS_OK) {
        display->health.suspended = 1u;
    }
    return status;
}

status_t display_device_resume(display_device_t *display)
{
    status_t status;

    if (display == 0 || display->health.initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = display->ops->resume != 0
        ? display->ops->resume(display->context) : SYS_OK;
    display->health.last_error = status;
    if (status == SYS_OK) {
        display->health.suspended = 0u;
    }
    return status;
}

status_t display_device_get_health(const display_device_t *display,
                                   display_device_health_t *health)
{
    if (display == 0 || health == 0) {
        return ERR_INVALID_ARG;
    }
    *health = display->health;
    return SYS_OK;
}
