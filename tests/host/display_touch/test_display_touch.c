#include "gt911.h"
#include "lcd_controller.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t id_words[4];
    uint16_t commands[128];
    uint16_t data[256];
    size_t command_count;
    size_t data_count;
    size_t read_index;
    size_t pixels_written;
    uint32_t delayed_ms;
} lcd_mock_t;

static status_t lcd_write_command(void *opaque, uint16_t command)
{
    lcd_mock_t *mock = opaque;

    assert(mock->command_count < sizeof(mock->commands) /
                                  sizeof(mock->commands[0]));
    mock->commands[mock->command_count++] = command;
    if (command == 0xD3u) {
        mock->read_index = 0u;
    }
    return SYS_OK;
}

static status_t lcd_write_data(void *opaque, uint16_t data)
{
    lcd_mock_t *mock = opaque;

    assert(mock->data_count < sizeof(mock->data) / sizeof(mock->data[0]));
    mock->data[mock->data_count++] = data;
    return SYS_OK;
}

static status_t lcd_read_data(void *opaque, uint16_t *data)
{
    lcd_mock_t *mock = opaque;

    if (mock->read_index >= 4u) {
        return ERR_IO;
    }
    *data = mock->id_words[mock->read_index++];
    return SYS_OK;
}

static status_t lcd_write_pixels(void *opaque, const uint16_t *pixels,
                                 size_t pixel_count)
{
    lcd_mock_t *mock = opaque;

    assert(pixels != 0);
    mock->pixels_written += pixel_count;
    return SYS_OK;
}

static void lcd_set_reset(void *opaque, uint8_t asserted)
{
    (void)opaque;
    (void)asserted;
}

static void lcd_delay(void *opaque, uint32_t delay_ms)
{
    lcd_mock_t *mock = opaque;

    mock->delayed_ms += delay_ms;
}

static const lcd_bus_ops_t lcd_ops = {
    lcd_write_command, lcd_write_data, lcd_read_data, lcd_write_pixels,
    lcd_set_reset, lcd_delay
};

static int command_seen(const lcd_mock_t *mock, uint16_t command)
{
    size_t i;

    for (i = 0u; i < mock->command_count; ++i) {
        if (mock->commands[i] == command) {
            return 1;
        }
    }
    return 0;
}

static void set_lcd_id(lcd_mock_t *mock, uint16_t controller_id)
{
    mock->id_words[0] = 0u;
    mock->id_words[1] = 0u;
    mock->id_words[2] = (uint16_t)(controller_id >> 8u);
    mock->id_words[3] = (uint16_t)(controller_id & 0xFFu);
}

static void test_lcd_dispatch_and_lifecycle(void)
{
    lcd_mock_t mock;
    lcd_bus_t bus;
    lcd_controller_t controller;
    const uint16_t pixels[4] = { 1u, 2u, 3u, 4u };

    memset(&mock, 0, sizeof(mock));
    assert(lcd_bus_construct(&bus, &lcd_ops, &mock) == SYS_OK);
    set_lcd_id(&mock, LCD_CONTROLLER_ID_ILI9806G);
    assert(lcd_controller_detect(&controller, &bus, 800u, 480u) == SYS_OK);
    assert(strcmp(lcd_controller_name(&controller), "ILI9806G") == 0);
    assert(lcd_controller_init(&controller) == SYS_OK);
    assert(command_seen(&mock, 0x11u));
    assert(command_seen(&mock, 0x29u));
    assert(lcd_controller_flush(&controller, 10u, 20u, 11u, 21u,
                                pixels, 4u) == SYS_OK);
    assert(mock.pixels_written == 4u);
    assert(command_seen(&mock, 0x2Au));
    assert(command_seen(&mock, 0x2Bu));
    assert(command_seen(&mock, 0x2Cu));
    assert(lcd_controller_flush(&controller, 799u, 0u, 800u, 0u,
                                pixels, 2u) == ERR_INVALID_ARG);
    assert(lcd_controller_flush(&controller, 0u, 0u, 1u, 1u,
                                pixels, 3u) == ERR_INVALID_ARG);
    assert(lcd_controller_suspend(&controller) == SYS_OK);
    assert(controller.suspended == 1u);
    assert(lcd_controller_resume(&controller) == SYS_OK);
    assert(controller.suspended == 0u);

    memset(&mock, 0, sizeof(mock));
    assert(lcd_bus_construct(&bus, &lcd_ops, &mock) == SYS_OK);
    set_lcd_id(&mock, LCD_CONTROLLER_ID_NT35510);
    assert(lcd_controller_detect(&controller, &bus, 800u, 480u) == SYS_OK);
    assert(strcmp(lcd_controller_name(&controller), "NT35510") == 0);
    assert(lcd_controller_init(&controller) == SYS_OK);
    assert(command_seen(&mock, 0xF000u));
    assert(command_seen(&mock, 0xF004u));
    assert(command_seen(&mock, 0xB000u));
    assert(command_seen(&mock, 0xB002u));
    assert(!command_seen(&mock, 0xF0u));
    assert(command_seen(&mock, 0x11u));

    memset(&mock, 0, sizeof(mock));
    assert(lcd_bus_construct(&bus, &lcd_ops, &mock) == SYS_OK);
    set_lcd_id(&mock, 0x1234u);
    assert(lcd_controller_detect(&controller, &bus, 800u, 480u) ==
           ERR_UNSUPPORTED);
    assert(strcmp(lcd_controller_name(&controller), "unidentified") == 0);
}

typedef struct {
    uint8_t product_id[4];
    uint8_t resolution[4];
    uint8_t status;
    uint8_t point[8];
    uint8_t selected_address;
    uint8_t wake_count;
    uint8_t clear_count;
    uint8_t sleep_count;
    uint8_t fail_next;
} gt_mock_t;

static status_t gt_write(void *opaque, uint8_t address,
                         const uint8_t *data, size_t length)
{
    gt_mock_t *mock = opaque;
    uint16_t reg;

    assert(address == mock->selected_address);
    if (mock->fail_next != 0u) {
        mock->fail_next = 0u;
        return ERR_IO;
    }
    assert(length == 3u);
    reg = (uint16_t)(((uint16_t)data[0] << 8u) | data[1]);
    if (reg == 0x814Eu && data[2] == 0u) {
        mock->clear_count++;
    }
    if (reg == 0x8040u && data[2] == 0x05u) {
        mock->sleep_count++;
    }
    return SYS_OK;
}

static status_t gt_read(void *opaque, uint8_t address,
                        uint8_t *data, size_t length)
{
    (void)opaque;
    (void)address;
    (void)data;
    (void)length;
    return ERR_UNSUPPORTED;
}

static status_t gt_write_read(void *opaque, uint8_t address,
                              const uint8_t *write_data,
                              size_t write_length, uint8_t *read_data,
                              size_t read_length)
{
    gt_mock_t *mock = opaque;
    uint16_t reg;

    assert(address == mock->selected_address);
    if (mock->fail_next != 0u) {
        mock->fail_next = 0u;
        return ERR_IO;
    }
    assert(write_length == 2u);
    reg = (uint16_t)(((uint16_t)write_data[0] << 8u) | write_data[1]);
    if (reg == 0x8140u && read_length == sizeof(mock->product_id)) {
        memcpy(read_data, mock->product_id, read_length);
    } else if (reg == 0x8048u && read_length == sizeof(mock->resolution)) {
        memcpy(read_data, mock->resolution, read_length);
    } else if (reg == 0x814Eu && read_length == 1u) {
        read_data[0] = mock->status;
    } else if (reg == 0x814Fu && read_length == sizeof(mock->point)) {
        memcpy(read_data, mock->point, read_length);
    } else {
        return ERR_PROTOCOL;
    }
    return SYS_OK;
}

static void gt_delay(void *opaque, uint32_t delay_ms)
{
    (void)opaque;
    (void)delay_ms;
}

static status_t gt_select_address(void *opaque, uint8_t address)
{
    gt_mock_t *mock = opaque;

    mock->selected_address = address;
    return SYS_OK;
}

static status_t gt_wake(void *opaque)
{
    gt_mock_t *mock = opaque;

    mock->wake_count++;
    return SYS_OK;
}

static const i2c_bus_ops_t gt_bus_ops = {
    gt_write, gt_read, gt_write_read, gt_delay
};

static const gt911_io_ops_t gt_io_ops = {
    gt_select_address, gt_wake
};

static void initialize_gt_mock(gt_mock_t *mock)
{
    memset(mock, 0, sizeof(*mock));
    memcpy(mock->product_id, "9110", 4u);
    mock->resolution[0] = 0x20u;
    mock->resolution[1] = 0x03u;
    mock->resolution[2] = 0xE0u;
    mock->resolution[3] = 0x01u;
}

static void test_gt911_input_and_faults(void)
{
    gt_mock_t mock;
    i2c_bus_t bus;
    gt911_t device;
    gt911_config_t config = {
        GT911_DEFAULT_ADDRESS, 800u, 480u, 1u, 1u, 0u
    };
    input_sample_t sample;

    initialize_gt_mock(&mock);
    assert(i2c_bus_construct(&bus, &gt_bus_ops, &mock, 20u) == SYS_OK);
    assert(gt911_construct(&device, &bus, &gt_io_ops, &mock, &config) ==
           SYS_OK);
    assert(gt911_init(&device) == SYS_OK);
    assert(mock.selected_address == GT911_DEFAULT_ADDRESS);
    assert(strcmp(device.product_id, "9110") == 0);
    assert(device.panel_width == 800u && device.panel_height == 480u);

    mock.status = 0x81u;
    mock.point[1] = 100u;
    mock.point[2] = 0u;
    mock.point[3] = 200u;
    mock.point[4] = 0u;
    assert(gt911_read(&device, &sample) == SYS_OK);
    assert(sample.state == INPUT_STATE_PRESSED);
    assert(sample.x == 465 && sample.y == 60);
    assert(mock.clear_count == 1u);

    mock.status = 0x80u;
    assert(gt911_read(&device, &sample) == SYS_OK);
    assert(sample.state == INPUT_STATE_RELEASED);
    assert(mock.clear_count == 2u);

    mock.status = 0x86u;
    assert(gt911_read(&device, &sample) == ERR_PROTOCOL);
    assert(mock.clear_count == 3u);

    mock.fail_next = 1u;
    assert(gt911_read(&device, &sample) == ERR_IO);
    assert(gt911_suspend(&device) == SYS_OK);
    assert(mock.sleep_count == 1u);
    assert(gt911_resume(&device) == SYS_OK);
    assert(mock.wake_count == 1u);
}

int main(void)
{
    test_lcd_dispatch_and_lifecycle();
    test_gt911_input_and_faults();
    puts("display/touch host tests passed");
    return 0;
}
