#include "f407_flash.h"

#include <string.h>

static const f407_flash_sector_t flash_sectors[F407_FLASH_SECTOR_COUNT] = {
    {0x08000000u, 16u * 1024u, 0u},
    {0x08004000u, 16u * 1024u, 1u},
    {0x08008000u, 16u * 1024u, 2u},
    {0x0800C000u, 16u * 1024u, 3u},
    {0x08010000u, 64u * 1024u, 4u},
    {0x08020000u, 128u * 1024u, 5u},
    {0x08040000u, 128u * 1024u, 6u},
    {0x08060000u, 128u * 1024u, 7u},
    {0x08080000u, 128u * 1024u, 8u},
    {0x080A0000u, 128u * 1024u, 9u},
    {0x080C0000u, 128u * 1024u, 10u},
    {0x080E0000u, 128u * 1024u, 11u}};

static int port_complete(const f407_flash_port_t *port)
{
    return port != 0 && port->unlock != 0 && port->lock != 0 &&
           port->erase_sector != 0 && port->program_word != 0 &&
           port->read != 0;
}

static int slot_valid(app_slot_t slot)
{
    return slot == SLOT_A || slot == SLOT_B;
}

static int range_inside(uint32_t address,
                        size_t length,
                        uint32_t region_start,
                        uint32_t region_size)
{
    uint32_t region_end;

    if (length == 0u || region_size > UINT32_MAX - region_start ||
        address < region_start) {
        return 0;
    }
    region_end = region_start + region_size;
    if (address >= region_end) {
        return 0;
    }
    return length <= (size_t)(region_end - address);
}

static status_t ensure_ready(const f407_flash_t *flash)
{
    return flash != 0 && flash->initialized != 0u ? SYS_OK : ERR_INVALID_ARG;
}

const f407_flash_sector_t *f407_flash_sector_get(uint8_t sector_index)
{
    return sector_index < F407_FLASH_SECTOR_COUNT ? &flash_sectors[sector_index]
                                                  : 0;
}

const f407_flash_sector_t *f407_flash_sector_for_address(uint32_t address)
{
    size_t i;

    for (i = 0u; i < F407_FLASH_SECTOR_COUNT; ++i) {
        uint32_t end = flash_sectors[i].start + flash_sectors[i].size;
        if (address >= flash_sectors[i].start && address < end) {
            return &flash_sectors[i];
        }
    }
    return 0;
}

status_t f407_flash_init(f407_flash_t *flash, const f407_flash_port_t *port)
{
    if (flash == 0 || !port_complete(port)) {
        return ERR_INVALID_ARG;
    }
    memset(flash, 0, sizeof(*flash));
    flash->port = *port;
    flash->initialized = 1u;
    return SYS_OK;
}

status_t f407_flash_unlock(f407_flash_t *flash)
{
    status_t status = ensure_ready(flash);

    return status == SYS_OK ? flash->port.unlock(flash->port.context) : status;
}

status_t f407_flash_lock(f407_flash_t *flash)
{
    status_t status = ensure_ready(flash);

    return status == SYS_OK ? flash->port.lock(flash->port.context) : status;
}

status_t f407_flash_read(f407_flash_t *flash,
                         uint32_t address,
                         uint8_t *buffer,
                         size_t length)
{
    status_t status = ensure_ready(flash);

    if (status != SYS_OK) {
        return status;
    }
    if (buffer == 0 ||
        !range_inside(
            address, length, INTERNAL_FLASH_BASE, INTERNAL_FLASH_SIZE)) {
        return ERR_INVALID_ARG;
    }
    return flash->port.read(flash->port.context, address, buffer, length);
}

static status_t
erase_exact_sectors(f407_flash_t *flash, uint32_t address, size_t length)
{
    const f407_flash_sector_t *sector;
    uint32_t current = address;
    uint32_t end;

    if (ensure_ready(flash) != SYS_OK || length > UINT32_MAX - address) {
        return ERR_INVALID_ARG;
    }
    end = address + (uint32_t)length;
    sector = f407_flash_sector_for_address(address);
    if (sector == 0 || sector->start != address) {
        return ERR_INVALID_ARG;
    }

    while (current < end) {
        status_t status;

        sector = f407_flash_sector_for_address(current);
        if (sector == 0 || sector->start != current ||
            sector->size > end - current) {
            return ERR_INVALID_ARG;
        }
        status = flash->port.erase_sector(flash->port.context, sector->index);
        if (status != SYS_OK) {
            return status;
        }
        current += sector->size;
    }
    return current == end ? SYS_OK : ERR_INVALID_ARG;
}

static status_t program_words(f407_flash_t *flash,
                              uint32_t address,
                              const uint8_t *data,
                              size_t length)
{
    size_t offset;

    if (ensure_ready(flash) != SYS_OK || data == 0 || length == 0u ||
        (address & 3u) != 0u) {
        return ERR_INVALID_ARG;
    }

    for (offset = 0u; offset < length; offset += sizeof(uint32_t)) {
        uint32_t value = UINT32_MAX;
        size_t chunk = length - offset;
        status_t status;

        if (chunk > sizeof(value)) {
            chunk = sizeof(value);
        }
        memcpy(&value, &data[offset], chunk);
        status = flash->port.program_word(
            flash->port.context, address + (uint32_t)offset, value);
        if (status != SYS_OK) {
            return status;
        }
    }
    return SYS_OK;
}

static const partition_t *inactive_partition(app_slot_t active_slot)
{
    if (!slot_valid(active_slot)) {
        return 0;
    }
    return partition_get_slot(active_slot == SLOT_A ? SLOT_B : SLOT_A);
}

status_t f407_flash_erase_inactive(f407_flash_t *flash,
                                   uint32_t address,
                                   size_t length,
                                   app_slot_t active_slot)
{
    const partition_t *inactive = inactive_partition(active_slot);

    if (inactive == 0) {
        return ERR_SLOT_MISMATCH;
    }
    if (!range_inside(address, length, inactive->start, inactive->size)) {
        return ERR_FLASH_ERASE;
    }
    return erase_exact_sectors(flash, address, length);
}

status_t f407_flash_program_inactive(f407_flash_t *flash,
                                     uint32_t address,
                                     const uint8_t *data,
                                     size_t length,
                                     app_slot_t active_slot)
{
    const partition_t *inactive = inactive_partition(active_slot);

    if (inactive == 0) {
        return ERR_SLOT_MISMATCH;
    }
    if (!range_inside(address, length, inactive->start, inactive->size)) {
        return ERR_FLASH_WRITE;
    }
    return program_words(flash, address, data, length);
}

status_t f407_flash_erase_metadata(f407_flash_t *flash, app_slot_t copy_slot)
{
    const partition_t *metadata = partition_get_metadata(copy_slot);

    if (metadata == 0) {
        return ERR_SLOT_MISMATCH;
    }
    return erase_exact_sectors(flash, metadata->start, metadata->size);
}

status_t f407_flash_program_metadata(f407_flash_t *flash,
                                     app_slot_t copy_slot,
                                     const uint8_t *data,
                                     size_t length)
{
    const partition_t *metadata = partition_get_metadata(copy_slot);

    if (metadata == 0) {
        return ERR_SLOT_MISMATCH;
    }
    if (length > metadata->size) {
        return ERR_FLASH_WRITE;
    }
    return program_words(flash, metadata->start, data, length);
}

status_t f407_flash_erase_descriptor(f407_flash_t *flash, app_slot_t slot)
{
    const partition_t *descriptor = partition_get_slot_descriptor(slot);

    if (descriptor == 0) {
        return ERR_SLOT_MISMATCH;
    }
    return erase_exact_sectors(flash, descriptor->start, descriptor->size);
}

status_t f407_flash_program_descriptor(f407_flash_t *flash,
                                       app_slot_t slot,
                                       const uint8_t *data,
                                       size_t length)
{
    const partition_t *descriptor = partition_get_slot_descriptor(slot);

    if (descriptor == 0) {
        return ERR_SLOT_MISMATCH;
    }
    if (length > descriptor->size) {
        return ERR_FLASH_WRITE;
    }
    return program_words(flash, descriptor->start, data, length);
}
