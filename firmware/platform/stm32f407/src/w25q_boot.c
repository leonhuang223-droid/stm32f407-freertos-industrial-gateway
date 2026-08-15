#include "w25q_boot.h"

#include <string.h>

#define W25Q_COMMAND_RELEASE_POWER_DOWN 0xABu
#define W25Q_COMMAND_READ_JEDEC_ID 0x9Fu
#define W25Q_COMMAND_READ_DATA 0x03u

static int port_complete(const w25q_boot_port_t *port)
{
    return port != 0 && port->select != 0 && port->transmit != 0 &&
           port->receive != 0 && port->delay_ms != 0;
}

static status_t finish_transaction(w25q_boot_t *device, status_t status)
{
    status_t deselect_status = device->port.select(
        device->port.context, 0);

    return status == SYS_OK ? deselect_status : status;
}

static status_t command_only(w25q_boot_t *device, uint8_t command)
{
    status_t status = device->port.select(device->port.context, 1);

    if (status == SYS_OK) {
        status = device->port.transmit(
            device->port.context, &command, sizeof(command));
    }
    return finish_transaction(device, status);
}

static status_t read_jedec_id(w25q_boot_t *device, uint32_t *out_id)
{
    uint8_t command = W25Q_COMMAND_READ_JEDEC_ID;
    uint8_t id[3];
    status_t status;

    status = device->port.select(device->port.context, 1);
    if (status == SYS_OK) {
        status = device->port.transmit(
            device->port.context, &command, sizeof(command));
    }
    if (status == SYS_OK) {
        status = device->port.receive(
            device->port.context, id, sizeof(id));
    }
    status = finish_transaction(device, status);
    if (status == SYS_OK) {
        *out_id = ((uint32_t)id[0] << 16u) |
                  ((uint32_t)id[1] << 8u) |
                  (uint32_t)id[2];
    }
    return status;
}

status_t w25q_boot_init(w25q_boot_t *device,
                        const w25q_boot_config_t *config,
                        const w25q_boot_port_t *port)
{
    status_t status;

    if (device == 0 || config == 0 || config->expected_jedec_id == 0u ||
        config->total_size == 0u || !port_complete(port)) {
        return ERR_INVALID_ARG;
    }
    memset(device, 0, sizeof(*device));
    device->port = *port;
    device->config = *config;

    status = command_only(device, W25Q_COMMAND_RELEASE_POWER_DOWN);
    if (status != SYS_OK) {
        return status;
    }
    device->port.delay_ms(device->port.context, 1u);
    status = read_jedec_id(device, &device->detected_jedec_id);
    if (status != SYS_OK) {
        return status;
    }
    if (device->detected_jedec_id != device->config.expected_jedec_id) {
        return ERR_UNSUPPORTED;
    }
    device->initialized = 1u;
    return SYS_OK;
}

status_t w25q_boot_read(w25q_boot_t *device, uint32_t address,
                        uint8_t *buffer, size_t length)
{
    uint8_t command[4];
    status_t status;

    if (device == 0 || device->initialized == 0u || buffer == 0 ||
        length == 0u || address >= device->config.total_size ||
        length > (size_t)(device->config.total_size - address)) {
        return ERR_INVALID_ARG;
    }

    command[0] = W25Q_COMMAND_READ_DATA;
    command[1] = (uint8_t)(address >> 16u);
    command[2] = (uint8_t)(address >> 8u);
    command[3] = (uint8_t)address;
    status = device->port.select(device->port.context, 1);
    if (status == SYS_OK) {
        status = device->port.transmit(
            device->port.context, command, sizeof(command));
    }
    if (status == SYS_OK) {
        status = device->port.receive(
            device->port.context, buffer, length);
    }
    return finish_transaction(device, status);
}

uint32_t w25q_boot_detected_jedec_id(const w25q_boot_t *device)
{
    return device != 0 ? device->detected_jedec_id : 0u;
}
