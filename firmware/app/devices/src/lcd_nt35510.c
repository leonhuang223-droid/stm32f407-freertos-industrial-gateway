#include "lcd_controller.h"

static status_t write_command_data(lcd_bus_t *bus,
                                   uint16_t command,
                                   const uint8_t *data,
                                   size_t data_length)
{
    size_t i;
    status_t status = lcd_bus_write_command(bus, command);

    for (i = 0u; status == SYS_OK && i < data_length; ++i) {
        status = lcd_bus_write_data(bus, data[i]);
    }
    return status;
}

static status_t write_indexed_registers(lcd_bus_t *bus,
                                        uint16_t first_register,
                                        const uint8_t *data,
                                        size_t data_length)
{
    size_t i;
    status_t status = SYS_OK;

    for (i = 0u; status == SYS_OK && i < data_length; ++i) {
        status = lcd_bus_write_command(
            bus, (uint16_t)(first_register + (uint16_t)i));
        if (status == SYS_OK) {
            status = lcd_bus_write_data(bus, data[i]);
        }
    }
    return status;
}

static status_t nt35510_probe(lcd_bus_t *bus, uint16_t *controller_id)
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
    return *controller_id == LCD_CONTROLLER_ID_NT35510 ? SYS_OK
                                                       : ERR_UNSUPPORTED;
}

static status_t nt35510_init(lcd_controller_t *controller)
{
    static const uint8_t page1[] = {0x55u, 0xAAu, 0x52u, 0x08u, 0x01u};
    static const uint8_t page0[] = {0x55u, 0xAAu, 0x52u, 0x08u, 0x00u};
    static const uint8_t avdd[] = {0x0Du, 0x0Du, 0x0Du};
    static const uint8_t avee[] = {0x34u, 0x34u, 0x34u};
    static const uint8_t vcl[] = {0x0Du, 0x0Du, 0x0Du};
    static const uint8_t pixel_format[] = {0x55u};
    static const uint8_t orientation[] = {0x28u};
    status_t status;

    status =
        write_indexed_registers(controller->bus, 0xF000u, page1, sizeof(page1));
    if (status == SYS_OK) {
        status = write_indexed_registers(
            controller->bus, 0xB000u, avdd, sizeof(avdd));
    }
    if (status == SYS_OK) {
        status = write_indexed_registers(
            controller->bus, 0xB100u, avee, sizeof(avee));
    }
    if (status == SYS_OK) {
        status =
            write_indexed_registers(controller->bus, 0xB200u, vcl, sizeof(vcl));
    }
    if (status == SYS_OK) {
        status = write_indexed_registers(
            controller->bus, 0xF000u, page0, sizeof(page0));
    }
    if (status == SYS_OK) {
        status = write_command_data(
            controller->bus, 0x3Au, pixel_format, sizeof(pixel_format));
    }
    if (status == SYS_OK) {
        status = write_command_data(
            controller->bus, 0x36u, orientation, sizeof(orientation));
    }
    if (status == SYS_OK) {
        status = lcd_bus_write_command(controller->bus, 0x11u);
        lcd_bus_delay(controller->bus, 120u);
    }
    if (status == SYS_OK) {
        status = lcd_bus_write_command(controller->bus, 0x29u);
        lcd_bus_delay(controller->bus, 20u);
    }
    return status;
}

static status_t nt35510_set_window(lcd_controller_t *controller,
                                   uint16_t x1,
                                   uint16_t y1,
                                   uint16_t x2,
                                   uint16_t y2)
{
    const uint8_t x_data[] = {
        (uint8_t)(x1 >> 8u), (uint8_t)x1, (uint8_t)(x2 >> 8u), (uint8_t)x2};
    const uint8_t y_data[] = {
        (uint8_t)(y1 >> 8u), (uint8_t)y1, (uint8_t)(y2 >> 8u), (uint8_t)y2};
    status_t status =
        write_command_data(controller->bus, 0x2Au, x_data, sizeof(x_data));

    if (status == SYS_OK) {
        status =
            write_command_data(controller->bus, 0x2Bu, y_data, sizeof(y_data));
    }
    if (status == SYS_OK) {
        status = lcd_bus_write_command(controller->bus, 0x2Cu);
    }
    return status;
}

static status_t nt35510_suspend(lcd_controller_t *controller)
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

static status_t nt35510_resume(lcd_controller_t *controller)
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

const lcd_controller_ops_t *lcd_nt35510_ops(void)
{
    static const lcd_controller_ops_t ops = {"NT35510",
                                             nt35510_probe,
                                             nt35510_init,
                                             nt35510_set_window,
                                             nt35510_suspend,
                                             nt35510_resume};

    return &ops;
}
