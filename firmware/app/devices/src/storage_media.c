#include "storage_media.h"

#include <string.h>

static int range_valid(const storage_media_t *media, uint32_t address,
                       size_t length)
{
    return media != 0 && length != 0u && address < media->total_size &&
           length <= (size_t)(media->total_size - address);
}

status_t storage_media_construct(storage_media_t *media,
                                 const storage_media_ops_t *ops,
                                 void *context, uint32_t total_size,
                                 uint32_t page_size, uint32_t sector_size)
{
    if (media == 0 || ops == 0 || context == 0 || ops->read == 0 ||
        ops->program == 0 || ops->erase_sector == 0 || ops->wake == 0 ||
        total_size == 0u || page_size == 0u || sector_size == 0u ||
        (sector_size % page_size) != 0u) {
        return ERR_INVALID_ARG;
    }
    memset(media, 0, sizeof(*media));
    media->ops = ops;
    media->context = context;
    media->total_size = total_size;
    media->page_size = page_size;
    media->sector_size = sector_size;
    return SYS_OK;
}

status_t storage_media_read(storage_media_t *media, uint32_t address,
                            uint8_t *buffer, size_t length)
{
    if (!range_valid(media, address, length) || buffer == 0) {
        return ERR_INVALID_ARG;
    }
    return media->ops->read(media->context, address, buffer, length);
}

status_t storage_media_program(storage_media_t *media, uint32_t address,
                               const uint8_t *data, size_t length)
{
    if (!range_valid(media, address, length) || data == 0) {
        return ERR_INVALID_ARG;
    }
    return media->ops->program(media->context, address, data, length);
}

status_t storage_media_erase_sector(storage_media_t *media,
                                    uint32_t address)
{
    if (media == 0 || media->ops == 0 || address >= media->total_size ||
        (address % media->sector_size) != 0u) {
        return ERR_INVALID_ARG;
    }
    return media->ops->erase_sector(media->context, address);
}

status_t storage_media_wake(storage_media_t *media)
{
    return media != 0 && media->ops != 0
        ? media->ops->wake(media->context) : ERR_INVALID_ARG;
}

status_t storage_media_power_down(storage_media_t *media)
{
    if (media == 0 || media->ops == 0) {
        return ERR_INVALID_ARG;
    }
    return media->ops->power_down != 0
        ? media->ops->power_down(media->context) : SYS_OK;
}
