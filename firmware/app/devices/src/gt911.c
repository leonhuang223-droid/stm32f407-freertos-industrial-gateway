#include "gt911.h"

#include <string.h>

#define GT911_REG_COMMAND 0x8040u
#define GT911_REG_X_OUTPUT_MAX 0x8048u
#define GT911_REG_PRODUCT_ID 0x8140u
#define GT911_REG_STATUS 0x814Eu
#define GT911_REG_FIRST_POINT 0x814Fu
#define GT911_COMMAND_SLEEP 0x05u

static status_t read_register(gt911_t *device, uint16_t reg,
                              uint8_t *data, size_t length)
{
    uint8_t address_bytes[2] = {
        (uint8_t)(reg >> 8u), (uint8_t)(reg & 0xFFu)
    };

    return i2c_bus_write_read(device->bus, device->config.address,
                              address_bytes, sizeof(address_bytes),
                              data, length);
}

static status_t write_register(gt911_t *device, uint16_t reg,
                               const uint8_t *data, size_t length)
{
    uint8_t buffer[3];

    if (data == 0 || length != 1u) {
        return ERR_INVALID_ARG;
    }
    buffer[0] = (uint8_t)(reg >> 8u);
    buffer[1] = (uint8_t)(reg & 0xFFu);
    buffer[2] = data[0];
    return i2c_bus_write(device->bus, device->config.address,
                         buffer, sizeof(buffer));
}

static status_t clear_status(gt911_t *device)
{
    const uint8_t clear = 0u;

    return write_register(device, GT911_REG_STATUS, &clear, 1u);
}

static status_t transform_point(const gt911_t *device,
                                uint16_t raw_x, uint16_t raw_y,
                                int16_t *logical_x, int16_t *logical_y)
{
    uint32_t x = raw_x;
    uint32_t y = raw_y;
    uint32_t source_width = device->panel_width;
    uint32_t source_height = device->panel_height;
    uint32_t temporary;

    if (raw_x >= device->panel_width || raw_y >= device->panel_height) {
        return ERR_PROTOCOL;
    }
    if (device->config.swap_xy != 0u) {
        temporary = x;
        x = y;
        y = temporary;
        temporary = source_width;
        source_width = source_height;
        source_height = temporary;
    }
    if (device->config.invert_x != 0u) {
        x = source_width - 1u - x;
    }
    if (device->config.invert_y != 0u) {
        y = source_height - 1u - y;
    }
    x = x * device->config.logical_width / source_width;
    y = y * device->config.logical_height / source_height;
    if (x >= device->config.logical_width) {
        x = device->config.logical_width - 1u;
    }
    if (y >= device->config.logical_height) {
        y = device->config.logical_height - 1u;
    }
    *logical_x = (int16_t)x;
    *logical_y = (int16_t)y;
    return SYS_OK;
}

status_t gt911_construct(gt911_t *device, i2c_bus_t *bus,
                         const gt911_io_ops_t *io_ops, void *io_context,
                         const gt911_config_t *config)
{
    if (device == 0 || bus == 0 || io_ops == 0 ||
        io_ops->select_address == 0 || io_ops->wake == 0 || config == 0 ||
        config->logical_width == 0u || config->logical_height == 0u ||
        (config->address != GT911_DEFAULT_ADDRESS &&
         config->address != 0x14u)) {
        return ERR_INVALID_ARG;
    }
    memset(device, 0, sizeof(*device));
    device->bus = bus;
    device->io_ops = io_ops;
    device->io_context = io_context;
    device->config = *config;
    device->last_sample.state = INPUT_STATE_RELEASED;
    return SYS_OK;
}

status_t gt911_init(gt911_t *device)
{
    uint8_t product_id[4];
    uint8_t resolution[4];
    status_t status;

    if (device == 0 || device->bus == 0 || device->io_ops == 0) {
        return ERR_INVALID_ARG;
    }
    status = device->io_ops->select_address(device->io_context,
                                             device->config.address);
    if (status == SYS_OK) {
        status = read_register(device, GT911_REG_PRODUCT_ID,
                               product_id, sizeof(product_id));
    }
    if (status == SYS_OK &&
        !(product_id[0] == '9' && product_id[1] == '1' &&
          product_id[2] == '1')) {
        status = ERR_UNSUPPORTED;
    }
    if (status == SYS_OK) {
        status = read_register(device, GT911_REG_X_OUTPUT_MAX,
                               resolution, sizeof(resolution));
    }
    if (status != SYS_OK) {
        return status;
    }
    device->panel_width = (uint16_t)(resolution[0] |
                                      ((uint16_t)resolution[1] << 8u));
    device->panel_height = (uint16_t)(resolution[2] |
                                       ((uint16_t)resolution[3] << 8u));
    if (device->panel_width == 0u || device->panel_height == 0u) {
        return ERR_PROTOCOL;
    }
    memcpy(device->product_id, product_id, sizeof(product_id));
    device->product_id[4] = '\0';
    device->initialized = 1u;
    device->suspended = 0u;
    return SYS_OK;
}

status_t gt911_read(gt911_t *device, input_sample_t *sample)
{
    uint8_t status_byte;
    uint8_t point[8];
    uint8_t touch_count;
    status_t status;

    if (device == 0 || sample == 0 || device->initialized == 0u ||
        device->suspended != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = read_register(device, GT911_REG_STATUS, &status_byte, 1u);
    if (status != SYS_OK) {
        return status;
    }
    if ((status_byte & 0x80u) == 0u) {
        *sample = device->last_sample;
        return SYS_OK;
    }
    touch_count = status_byte & 0x0Fu;
    if (touch_count > GT911_MAX_TOUCHES) {
        (void)clear_status(device);
        return ERR_PROTOCOL;
    }
    if (touch_count == 0u) {
        device->last_sample.state = INPUT_STATE_RELEASED;
        status = clear_status(device);
        *sample = device->last_sample;
        return status;
    }
    status = read_register(device, GT911_REG_FIRST_POINT,
                           point, sizeof(point));
    if (status == SYS_OK) {
        const uint16_t raw_x = (uint16_t)(point[1] |
                                          ((uint16_t)point[2] << 8u));
        const uint16_t raw_y = (uint16_t)(point[3] |
                                          ((uint16_t)point[4] << 8u));

        status = transform_point(device, raw_x, raw_y,
                                 &device->last_sample.x,
                                 &device->last_sample.y);
    }
    if (status == SYS_OK) {
        device->last_sample.state = INPUT_STATE_PRESSED;
    }
    {
        status_t clear_result = clear_status(device);

        if (status == SYS_OK) {
            status = clear_result;
        }
    }
    if (status == SYS_OK) {
        *sample = device->last_sample;
    }
    return status;
}

status_t gt911_suspend(gt911_t *device)
{
    const uint8_t command = GT911_COMMAND_SLEEP;
    status_t status;

    if (device == 0 || device->initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if (device->suspended != 0u) {
        return SYS_OK;
    }
    status = write_register(device, GT911_REG_COMMAND, &command, 1u);
    if (status == SYS_OK) {
        device->suspended = 1u;
        device->last_sample.state = INPUT_STATE_RELEASED;
    }
    return status;
}

status_t gt911_resume(gt911_t *device)
{
    status_t status;

    if (device == 0 || device->initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if (device->suspended == 0u) {
        return SYS_OK;
    }
    status = device->io_ops->wake(device->io_context);
    if (status == SYS_OK) {
        device->suspended = 0u;
    }
    return status;
}
