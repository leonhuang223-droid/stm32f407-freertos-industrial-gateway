#include "f407_display_port.h"

#include "f407_gt911_port.h"
#include "f407_lcd_bus.h"
#include "lcd_controller.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

typedef struct {
    lcd_bus_t bus;
    lcd_controller_t controller;
    uint8_t requested_backlight;
} f407_display_context_t;

typedef struct {
    gt911_t controller;
} f407_input_context_t;

static f407_display_context_t display_context;
static f407_input_context_t input_context;

static status_t display_init(void *context, uint16_t width, uint16_t height)
{
    f407_display_context_t *display = context;
    status_t status;

    if (display == 0 || width != F407_LCD_WIDTH || height != F407_LCD_HEIGHT) {
        return ERR_INVALID_ARG;
    }
    lcd_bus_reset(&display->bus, 1u);
    lcd_bus_delay(&display->bus, 10u);
    lcd_bus_reset(&display->bus, 0u);
    lcd_bus_delay(&display->bus, 120u);
    status = lcd_controller_detect(
        &display->controller, &display->bus, width, height);
    if (status == SYS_OK) {
        status = lcd_controller_init(&display->controller);
    }
    return status;
}

static status_t display_flush(void *context,
                              const display_area_t *area,
                              const uint16_t *pixels,
                              size_t pixel_count)
{
    f407_display_context_t *display = context;

    if (display == 0 || area == 0 || pixels == 0 || area->x1 < 0 ||
        area->y1 < 0) {
        return ERR_INVALID_ARG;
    }
    return lcd_controller_flush(&display->controller,
                                &(const lcd_flush_request_t){(uint16_t)area->x1,
                                                             (uint16_t)area->y1,
                                                             (uint16_t)area->x2,
                                                             (uint16_t)area->y2,
                                                             pixels,
                                                             pixel_count});
}

static status_t display_set_backlight(void *context, uint8_t percent)
{
    f407_display_context_t *display = context;
    status_t status;

    if (display == 0 || percent > 100u) {
        return ERR_INVALID_ARG;
    }
    status = f407_lcd_backlight_set(percent);
    if (status == SYS_OK) {
        display->requested_backlight = percent;
    }
    return status;
}

static status_t display_suspend(void *context)
{
    f407_display_context_t *display = context;
    status_t status;

    if (display == 0) {
        return ERR_INVALID_ARG;
    }
    status = f407_lcd_backlight_set(0u);
    if (status == SYS_OK) {
        status = lcd_controller_suspend(&display->controller);
    }
    return status;
}

static status_t display_resume(void *context)
{
    f407_display_context_t *display = context;
    status_t status;

    if (display == 0) {
        return ERR_INVALID_ARG;
    }
    status = lcd_controller_resume(&display->controller);
    if (status == SYS_OK) {
        status = f407_lcd_backlight_set(display->requested_backlight);
    }
    return status;
}

static const display_device_ops_t display_ops = {display_init,
                                                 display_flush,
                                                 display_set_backlight,
                                                 display_suspend,
                                                 display_resume};

static status_t input_init(void *context, uint16_t width, uint16_t height)
{
    f407_input_context_t *input = context;

    if (input == 0 || width != F407_LCD_WIDTH || height != F407_LCD_HEIGHT) {
        return ERR_INVALID_ARG;
    }
    f407_gt911_port_bind_current_task();
    return gt911_init(&input->controller);
}

static status_t input_read(void *context, input_sample_t *sample)
{
    f407_input_context_t *input = context;
    status_t status;

    if (input == 0 || sample == 0) {
        return ERR_INVALID_ARG;
    }
    (void)f407_gt911_port_take_interrupt();
    status = gt911_read(&input->controller, sample);
    if (status == SYS_OK) {
        sample->timestamp_ms =
            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    }
    return status;
}

static status_t input_suspend(void *context)
{
    f407_input_context_t *input = context;

    return input != 0 ? gt911_suspend(&input->controller) : ERR_INVALID_ARG;
}

static status_t input_resume(void *context)
{
    f407_input_context_t *input = context;

    return input != 0 ? gt911_resume(&input->controller) : ERR_INVALID_ARG;
}

static const input_device_ops_t input_ops = {
    input_init, input_read, input_suspend, input_resume};

status_t f407_display_configure(app_context_t *context)
{
    app_ui_config_t config = {0};
    status_t status;

    if (context == 0) {
        return ERR_INVALID_ARG;
    }
    memset(&display_context, 0, sizeof(display_context));
    memset(&input_context, 0, sizeof(input_context));
    display_context.requested_backlight = 80u;
    status = f407_lcd_bus_configure(&display_context.bus);
    if (status == SYS_OK) {
        status = f407_gt911_port_construct(&input_context.controller);
    }
    if (status != SYS_OK) {
        return status;
    }

    config.display_ops = &display_ops;
    config.display_context = &display_context;
    config.input_ops = &input_ops;
    config.input_context = &input_context;
    config.width = F407_LCD_WIDTH;
    config.height = F407_LCD_HEIGHT;
    if (f407_external_sram_self_test() == SYS_OK) {
        config.draw_buffer_primary = f407_external_draw_buffer(0u);
        config.draw_buffer_secondary = f407_external_draw_buffer(1u);
        config.draw_buffer_pixels = F407_EXTERNAL_DRAW_BUFFER_PIXELS;
    } else {
        config.draw_buffer_degraded = 1u;
    }
    return app_context_configure_ui(context, &config);
}
