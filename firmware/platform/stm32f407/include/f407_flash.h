#ifndef F407_FLASH_H
#define F407_FLASH_H

#include "error_code.h"
#include "partition_table.h"

#include <stddef.h>
#include <stdint.h>

#define F407_FLASH_SECTOR_COUNT 12u

typedef struct {
    uint32_t start;
    uint32_t size;
    uint8_t index;
} f407_flash_sector_t;

typedef struct {
    status_t (*unlock)(void *context);
    status_t (*lock)(void *context);
    status_t (*erase_sector)(void *context, uint8_t sector_index);
    status_t (*program_word)(void *context, uint32_t address, uint32_t value);
    status_t (*read)(void *context,
                     uint32_t address,
                     uint8_t *buffer,
                     size_t length);
    void *context;
} f407_flash_port_t;

typedef struct {
    f407_flash_port_t port;
    uint8_t initialized;
} f407_flash_t;

const f407_flash_sector_t *f407_flash_sector_get(uint8_t sector_index);
const f407_flash_sector_t *f407_flash_sector_for_address(uint32_t address);

status_t f407_flash_init(f407_flash_t *flash, const f407_flash_port_t *port);
status_t f407_flash_unlock(f407_flash_t *flash);
status_t f407_flash_lock(f407_flash_t *flash);
status_t f407_flash_read(f407_flash_t *flash,
                         uint32_t address,
                         uint8_t *buffer,
                         size_t length);

status_t f407_flash_erase_inactive(f407_flash_t *flash,
                                   uint32_t address,
                                   size_t length,
                                   app_slot_t active_slot);
status_t f407_flash_program_inactive(f407_flash_t *flash,
                                     uint32_t address,
                                     const uint8_t *data,
                                     size_t length,
                                     app_slot_t active_slot);

status_t f407_flash_erase_metadata(f407_flash_t *flash, app_slot_t copy_slot);
status_t f407_flash_program_metadata(f407_flash_t *flash,
                                     app_slot_t copy_slot,
                                     const uint8_t *data,
                                     size_t length);

status_t f407_flash_erase_descriptor(f407_flash_t *flash, app_slot_t slot);
status_t f407_flash_program_descriptor(f407_flash_t *flash,
                                       app_slot_t slot,
                                       const uint8_t *data,
                                       size_t length);

#endif
