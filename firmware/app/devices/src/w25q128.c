#include "w25q128.h"

#include <string.h>

#define W25Q_COMMAND_WRITE_ENABLE 0x06u
#define W25Q_COMMAND_READ_STATUS_1 0x05u
#define W25Q_COMMAND_PAGE_PROGRAM 0x02u
#define W25Q_COMMAND_READ_DATA 0x03u
#define W25Q_COMMAND_SECTOR_ERASE 0x20u
#define W25Q_COMMAND_POWER_DOWN 0xB9u
#define W25Q_COMMAND_RELEASE_POWER_DOWN 0xABu
#define W25Q_COMMAND_READ_JEDEC_ID 0x9Fu
#define W25Q_STATUS_BUSY 0x01u
#define W25Q_STATUS_WRITE_ENABLE_LATCH 0x02u
#define W25Q_MAX_TRANSFER (EXTERNAL_FLASH_PAGE_SIZE + 4u)

static status_t raw_transfer(w25q128_t *device, uint8_t *tx, uint8_t *rx,
                             size_t length)
{
    return spi_device_transfer(device->spi, tx, rx, length);
}

static status_t command_only(w25q128_t *device, uint8_t command)
{
    return raw_transfer(device, &command, 0, 1u);
}

static status_t read_status(w25q128_t *device, uint8_t *status_register)
{
    uint8_t tx[2] = { W25Q_COMMAND_READ_STATUS_1, 0xffu };
    uint8_t rx[2] = { 0u, 0u };
    status_t status;

    if (status_register == 0) {
        return ERR_INVALID_ARG;
    }
    status = raw_transfer(device, tx, rx, sizeof(tx));
    if (status == SYS_OK) {
        *status_register = rx[1];
    }
    return status;
}

static status_t wait_ready(w25q128_t *device)
{
    uint32_t elapsed;

    for (elapsed = 0u; elapsed < device->config.operation_timeout_ms;
         ++elapsed) {
        uint8_t status_register = 0u;
        status_t status = read_status(device, &status_register);

        if (status != SYS_OK) {
            return status;
        }
        if ((status_register & W25Q_STATUS_BUSY) == 0u) {
            return SYS_OK;
        }
        spi_device_delay(device->spi, 1u);
    }
    return ERR_TIMEOUT;
}

static status_t write_enable(w25q128_t *device)
{
    uint8_t status_register = 0u;
    status_t status = command_only(device, W25Q_COMMAND_WRITE_ENABLE);

    if (status == SYS_OK) {
        status = read_status(device, &status_register);
    }
    if (status == SYS_OK &&
        (status_register & W25Q_STATUS_WRITE_ENABLE_LATCH) == 0u) {
        status = ERR_FLASH_WRITE;
    }
    return status;
}

static status_t real_wake(w25q128_t *device)
{
    status_t status;

    if (device == 0 || device->spi == 0) {
        return ERR_INVALID_ARG;
    }
    status = command_only(device, W25Q_COMMAND_RELEASE_POWER_DOWN);
    if (status == SYS_OK) {
        spi_device_delay(device->spi, 1u);
        device->health.powered_down = 0u;
    }
    return status;
}

static status_t real_init(w25q128_t *device)
{
    uint8_t tx[4] = { W25Q_COMMAND_READ_JEDEC_ID, 0xffu, 0xffu, 0xffu };
    uint8_t rx[4] = { 0u, 0u, 0u, 0u };
    status_t status = real_wake(device);

    if (status == SYS_OK) {
        status = raw_transfer(device, tx, rx, sizeof(tx));
    }
    if (status == SYS_OK) {
        device->health.detected_jedec_id =
            ((uint32_t)rx[1] << 16u) |
            ((uint32_t)rx[2] << 8u) | rx[3];
        if (device->health.detected_jedec_id !=
            device->config.expected_jedec_id) {
            status = ERR_UNSUPPORTED;
        }
    }
    device->health.initialized = status == SYS_OK ? 1u : 0u;
    return status;
}

static int range_valid(const w25q128_t *device, uint32_t address,
                       size_t length)
{
    return device != 0 && device->health.initialized != 0u &&
           device->health.powered_down == 0u && length != 0u &&
           address < device->config.total_size &&
           length <= (size_t)(device->config.total_size - address);
}

static status_t real_read(w25q128_t *device, uint32_t address,
                          uint8_t *buffer, size_t length)
{
    uint8_t tx[W25Q_MAX_TRANSFER];
    uint8_t rx[W25Q_MAX_TRANSFER];
    size_t offset = 0u;

    if (!range_valid(device, address, length) || buffer == 0) {
        return ERR_INVALID_ARG;
    }
    while (offset < length) {
        size_t chunk = length - offset;
        status_t status;

        if (chunk > EXTERNAL_FLASH_PAGE_SIZE) {
            chunk = EXTERNAL_FLASH_PAGE_SIZE;
        }
        tx[0] = W25Q_COMMAND_READ_DATA;
        tx[1] = (uint8_t)((address + offset) >> 16u);
        tx[2] = (uint8_t)((address + offset) >> 8u);
        tx[3] = (uint8_t)(address + offset);
        memset(&tx[4], 0xff, chunk);
        status = raw_transfer(device, tx, rx, chunk + 4u);
        if (status != SYS_OK) {
            return status;
        }
        memcpy(&buffer[offset], &rx[4], chunk);
        offset += chunk;
    }
    return SYS_OK;
}

static status_t real_program(w25q128_t *device, uint32_t address,
                             const uint8_t *data, size_t length)
{
    uint8_t tx[W25Q_MAX_TRANSFER];
    size_t offset = 0u;

    if (!range_valid(device, address, length) || data == 0) {
        return ERR_INVALID_ARG;
    }
    while (offset < length) {
        uint32_t current_address = address + (uint32_t)offset;
        size_t page_remaining = EXTERNAL_FLASH_PAGE_SIZE -
            (current_address % EXTERNAL_FLASH_PAGE_SIZE);
        size_t chunk = length - offset;
        status_t status;

        if (chunk > page_remaining) {
            chunk = page_remaining;
        }
        status = write_enable(device);
        if (status != SYS_OK) {
            return status;
        }
        tx[0] = W25Q_COMMAND_PAGE_PROGRAM;
        tx[1] = (uint8_t)(current_address >> 16u);
        tx[2] = (uint8_t)(current_address >> 8u);
        tx[3] = (uint8_t)current_address;
        memcpy(&tx[4], &data[offset], chunk);
        status = raw_transfer(device, tx, 0, chunk + 4u);
        if (status == SYS_OK) {
            status = wait_ready(device);
        }
        if (status != SYS_OK) {
            return status;
        }
        offset += chunk;
    }
    return SYS_OK;
}

static status_t real_erase_sector(w25q128_t *device, uint32_t address)
{
    uint8_t tx[4];
    status_t status;

    if (!range_valid(device, address, EXTERNAL_FLASH_SECTOR_SIZE) ||
        (address % EXTERNAL_FLASH_SECTOR_SIZE) != 0u) {
        return ERR_INVALID_ARG;
    }
    status = write_enable(device);
    if (status != SYS_OK) {
        return status;
    }
    tx[0] = W25Q_COMMAND_SECTOR_ERASE;
    tx[1] = (uint8_t)(address >> 16u);
    tx[2] = (uint8_t)(address >> 8u);
    tx[3] = (uint8_t)address;
    status = raw_transfer(device, tx, 0, sizeof(tx));
    return status == SYS_OK ? wait_ready(device) : status;
}

static status_t real_power_down(w25q128_t *device)
{
    status_t status;

    if (device == 0 || device->health.initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = wait_ready(device);
    if (status == SYS_OK) {
        status = command_only(device, W25Q_COMMAND_POWER_DOWN);
    }
    if (status == SYS_OK) {
        device->health.powered_down = 1u;
    }
    return status;
}

static const w25q128_ops_t w25q_ops = {
    real_init, real_read, real_program, real_erase_sector,
    real_power_down, real_wake
};

static status_t record_result(w25q128_t *device, status_t status)
{
    device->health.last_error = status;
    if (status != SYS_OK) {
        device->health.failures++;
    }
    return status;
}

status_t w25q128_construct(w25q128_t *device, spi_device_t *spi,
                           const w25q128_config_t *config)
{
    if (device == 0 || spi == 0 || config == 0 ||
        config->expected_jedec_id == 0u || config->total_size == 0u ||
        config->total_size > 0x01000000u ||
        config->operation_timeout_ms == 0u) {
        return ERR_INVALID_ARG;
    }
    memset(device, 0, sizeof(*device));
    device->ops = &w25q_ops;
    device->spi = spi;
    device->config = *config;
    device->health.last_error = ERR_DEVICE_NOT_READY;
    return SYS_OK;
}

status_t w25q128_init(w25q128_t *device)
{
    return device != 0 && device->ops != 0
        ? record_result(device, device->ops->init(device)) : ERR_INVALID_ARG;
}

status_t w25q128_read(w25q128_t *device, uint32_t address,
                      uint8_t *buffer, size_t length)
{
    status_t status;

    if (device == 0 || device->ops == 0) {
        return ERR_INVALID_ARG;
    }
    status = device->ops->read(device, address, buffer, length);
    if (status == SYS_OK) {
        device->health.reads++;
    }
    return record_result(device, status);
}

status_t w25q128_program(w25q128_t *device, uint32_t address,
                         const uint8_t *data, size_t length)
{
    status_t status;

    if (device == 0 || device->ops == 0) {
        return ERR_INVALID_ARG;
    }
    status = device->ops->program(device, address, data, length);
    if (status == SYS_OK) {
        device->health.programs++;
    }
    return record_result(device, status);
}

status_t w25q128_erase_sector(w25q128_t *device, uint32_t address)
{
    status_t status;

    if (device == 0 || device->ops == 0) {
        return ERR_INVALID_ARG;
    }
    status = device->ops->erase_sector(device, address);
    if (status == SYS_OK) {
        device->health.erases++;
    }
    return record_result(device, status);
}

status_t w25q128_power_down(w25q128_t *device)
{
    return device != 0 && device->ops != 0
        ? record_result(device, device->ops->power_down(device))
        : ERR_INVALID_ARG;
}

status_t w25q128_wake(w25q128_t *device)
{
    status_t status;

    if (device == 0 || device->ops == 0) {
        return ERR_INVALID_ARG;
    }
    status = device->health.initialized != 0u
        ? device->ops->wake(device) : device->ops->init(device);
    return record_result(device, status);
}

status_t w25q128_get_health(const w25q128_t *device,
                            w25q128_health_t *health)
{
    if (device == 0 || health == 0) {
        return ERR_INVALID_ARG;
    }
    *health = device->health;
    return SYS_OK;
}

static status_t media_read(void *context, uint32_t address,
                           uint8_t *buffer, size_t length)
{
    return w25q128_read(context, address, buffer, length);
}

static status_t media_program(void *context, uint32_t address,
                              const uint8_t *data, size_t length)
{
    return w25q128_program(context, address, data, length);
}

static status_t media_erase(void *context, uint32_t address)
{
    return w25q128_erase_sector(context, address);
}

static status_t media_wake(void *context)
{
    return w25q128_wake(context);
}

static status_t media_power_down(void *context)
{
    return w25q128_power_down(context);
}

const storage_media_ops_t *w25q128_storage_media_ops(void)
{
    static const storage_media_ops_t ops = {
        media_read, media_program, media_erase, media_wake, media_power_down
    };

    return &ops;
}
