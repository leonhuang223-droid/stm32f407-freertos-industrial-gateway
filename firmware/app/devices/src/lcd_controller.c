#include "lcd_controller.h"

#include <string.h>

status_t lcd_controller_detect(lcd_controller_t *controller, lcd_bus_t *bus,
                               uint16_t width, uint16_t height)
{
    const lcd_controller_ops_t *candidates[] = {
        lcd_ili9806g_ops(), lcd_nt35510_ops()
    };
    size_t i;

    if (controller == 0 || bus == 0 || bus->initialized == 0u ||
        width == 0u || height == 0u) {
        return ERR_INVALID_ARG;
    }
    memset(controller, 0, sizeof(*controller));
    controller->bus = bus;
    controller->width = width;
    controller->height = height;
    for (i = 0u; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        uint16_t detected_id = 0u;
        status_t status = candidates[i]->probe(bus, &detected_id);

        if (status == SYS_OK) {
            controller->ops = candidates[i];
            controller->controller_id = detected_id;
            return SYS_OK;
        }
        if (status != ERR_UNSUPPORTED) {
            return status;
        }
    }
    return ERR_UNSUPPORTED;
}

status_t lcd_controller_init(lcd_controller_t *controller)
{
    status_t status;

    if (controller == 0 || controller->ops == 0 ||
        controller->ops->init == 0) {
        return ERR_DEVICE_NOT_READY;
    }
    status = controller->ops->init(controller);
    if (status == SYS_OK) {
        controller->initialized = 1u;
        controller->suspended = 0u;
    }
    return status;
}

status_t lcd_controller_flush(lcd_controller_t *controller,
                              uint16_t x1, uint16_t y1,
                              uint16_t x2, uint16_t y2,
                              const uint16_t *pixels, size_t pixel_count)
{
    size_t expected;
    status_t status;

    if (controller == 0 || pixels == 0 || controller->initialized == 0u ||
        controller->suspended != 0u || x1 > x2 || y1 > y2 ||
        x2 >= controller->width || y2 >= controller->height) {
        return ERR_INVALID_ARG;
    }
    expected = (size_t)(x2 - x1 + 1u) * (size_t)(y2 - y1 + 1u);
    if (pixel_count != expected) {
        return ERR_INVALID_ARG;
    }
    status = controller->ops->set_window(controller, x1, y1, x2, y2);
    if (status == SYS_OK) {
        status = lcd_bus_write_pixels(controller->bus, pixels, pixel_count);
    }
    return status;
}

status_t lcd_controller_suspend(lcd_controller_t *controller)
{
    status_t status;

    if (controller == 0 || controller->initialized == 0u ||
        controller->ops == 0) {
        return ERR_DEVICE_NOT_READY;
    }
    if (controller->suspended != 0u) {
        return SYS_OK;
    }
    status = controller->ops->suspend(controller);
    if (status == SYS_OK) {
        controller->suspended = 1u;
    }
    return status;
}

status_t lcd_controller_resume(lcd_controller_t *controller)
{
    status_t status;

    if (controller == 0 || controller->initialized == 0u ||
        controller->ops == 0) {
        return ERR_DEVICE_NOT_READY;
    }
    if (controller->suspended == 0u) {
        return SYS_OK;
    }
    status = controller->ops->resume(controller);
    if (status == SYS_OK) {
        controller->suspended = 0u;
    }
    return status;
}

const char *lcd_controller_name(const lcd_controller_t *controller)
{
    return controller != 0 && controller->ops != 0
        ? controller->ops->name : "unidentified";
}
