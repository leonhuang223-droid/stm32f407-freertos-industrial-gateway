#include "lcd_controller.h"

typedef struct {
    uint16_t command;
    const uint8_t *data;
    uint8_t data_length;
    uint16_t delay_ms;
} lcd_init_step_t;

static status_t write_step(lcd_bus_t *bus, const lcd_init_step_t *step)
{
    uint8_t i;
    status_t status = lcd_bus_write_command(bus, step->command);

    for (i = 0u; status == SYS_OK && i < step->data_length; ++i) {
        status = lcd_bus_write_data(bus, step->data[i]);
    }
    if (status == SYS_OK && step->delay_ms != 0u) {
        lcd_bus_delay(bus, step->delay_ms);
    }
    return status;
}

static status_t ili9806g_probe(lcd_bus_t *bus, uint16_t *controller_id)
{
    uint16_t id_data[4];
    status_t status;

    if (controller_id == 0) {
        return ERR_INVALID_ARG;
    }
    status = lcd_bus_read_register(bus, 0xD3u, id_data, 4u);
    if (status != SYS_OK) {
        return status;
    }
    *controller_id = (uint16_t)((id_data[2] << 8u) | id_data[3]);
    return *controller_id == LCD_CONTROLLER_ID_ILI9806G
        ? SYS_OK : ERR_UNSUPPORTED;
}

static status_t ili9806g_init(lcd_controller_t *controller)
{
    static const uint8_t page1[] = { 0xFFu, 0x98u, 0x06u, 0x04u, 0x01u };
    static const uint8_t interface_mode[] = { 0x10u };
    static const uint8_t inversion[] = { 0x01u };
    static const uint8_t resolution[] = { 0x01u };
    static const uint8_t vreg1[] = { 0x16u };
    static const uint8_t vreg2[] = { 0x33u };
    static const uint8_t vcom1[] = { 0x00u };
    static const uint8_t vcom2[] = { 0x48u };
    static const uint8_t pixel_format[] = { 0x55u };
    static const uint8_t orientation[] = { 0x28u };
    static const lcd_init_step_t steps[] = {
        { 0xFFu, page1, sizeof(page1), 0u },
        { 0x08u, interface_mode, sizeof(interface_mode), 0u },
        { 0x21u, inversion, sizeof(inversion), 0u },
        { 0x30u, resolution, sizeof(resolution), 0u },
        { 0x40u, vreg1, sizeof(vreg1), 0u },
        { 0x41u, vreg2, sizeof(vreg2), 0u },
        { 0x50u, vcom1, sizeof(vcom1), 0u },
        { 0x51u, vcom2, sizeof(vcom2), 0u },
        { 0x3Au, pixel_format, sizeof(pixel_format), 0u },
        { 0x36u, orientation, sizeof(orientation), 0u },
        { 0x11u, 0, 0u, 120u },
        { 0x29u, 0, 0u, 20u }
    };
    size_t i;
    status_t status = SYS_OK;

    for (i = 0u; status == SYS_OK && i < sizeof(steps) / sizeof(steps[0]);
         ++i) {
        status = write_step(controller->bus, &steps[i]);
    }
    return status;
}

static status_t ili9806g_set_window(lcd_controller_t *controller,
                                    uint16_t x1, uint16_t y1,
                                    uint16_t x2, uint16_t y2)
{
    const uint16_t values[] = {
        (uint16_t)(x1 >> 8u), (uint16_t)(x1 & 0xFFu),
        (uint16_t)(x2 >> 8u), (uint16_t)(x2 & 0xFFu),
        (uint16_t)(y1 >> 8u), (uint16_t)(y1 & 0xFFu),
        (uint16_t)(y2 >> 8u), (uint16_t)(y2 & 0xFFu)
    };
    size_t i;
    status_t status = lcd_bus_write_command(controller->bus, 0x2Au);

    for (i = 0u; status == SYS_OK && i < 4u; ++i) {
        status = lcd_bus_write_data(controller->bus, values[i]);
    }
    if (status == SYS_OK) {
        status = lcd_bus_write_command(controller->bus, 0x2Bu);
    }
    for (i = 4u; status == SYS_OK && i < 8u; ++i) {
        status = lcd_bus_write_data(controller->bus, values[i]);
    }
    if (status == SYS_OK) {
        status = lcd_bus_write_command(controller->bus, 0x2Cu);
    }
    return status;
}

static status_t ili9806g_suspend(lcd_controller_t *controller)
{
    status_t status = lcd_bus_write_command(controller->bus, 0x28u);

    if (status == SYS_OK) {
        lcd_bus_delay(controller->bus, 20u);
        status = lcd_bus_write_command(controller->bus, 0x10u);
    }
    if (status == SYS_OK) {
        lcd_bus_delay(controller->bus, 120u);
    }
    return status;
}

static status_t ili9806g_resume(lcd_controller_t *controller)
{
    status_t status = lcd_bus_write_command(controller->bus, 0x11u);

    if (status == SYS_OK) {
        lcd_bus_delay(controller->bus, 120u);
        status = lcd_bus_write_command(controller->bus, 0x29u);
    }
    if (status == SYS_OK) {
        lcd_bus_delay(controller->bus, 20u);
    }
    return status;
}

const lcd_controller_ops_t *lcd_ili9806g_ops(void)
{
    static const lcd_controller_ops_t ops = {
        "ILI9806G", ili9806g_probe, ili9806g_init, ili9806g_set_window,
        ili9806g_suspend, ili9806g_resume
    };

    return &ops;
}
