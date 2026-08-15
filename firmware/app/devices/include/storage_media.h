#ifndef GATEWAY_STORAGE_MEDIA_H
#define GATEWAY_STORAGE_MEDIA_H

#include "error_code.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    status_t (*read)(void *context, uint32_t address,
                     uint8_t *buffer, size_t length);
    status_t (*program)(void *context, uint32_t address,
                        const uint8_t *data, size_t length);
    status_t (*erase_sector)(void *context, uint32_t address);
    status_t (*wake)(void *context);
    status_t (*power_down)(void *context);
} storage_media_ops_t;

typedef struct {
    const storage_media_ops_t *ops;
    void *context;
    uint32_t total_size;
    uint32_t page_size;
    uint32_t sector_size;
} storage_media_t;

status_t storage_media_construct(storage_media_t *media,
                                 const storage_media_ops_t *ops,
                                 void *context, uint32_t total_size,
                                 uint32_t page_size, uint32_t sector_size);
status_t storage_media_read(storage_media_t *media, uint32_t address,
                            uint8_t *buffer, size_t length);
status_t storage_media_program(storage_media_t *media, uint32_t address,
                               const uint8_t *data, size_t length);
status_t storage_media_erase_sector(storage_media_t *media,
                                    uint32_t address);
status_t storage_media_wake(storage_media_t *media);
status_t storage_media_power_down(storage_media_t *media);

#endif
