#include "external_flash_layout.h"

static const external_flash_partition_t partitions[] = {
    { EXTERNAL_FLASH_OTA_STAGING_START, EXTERNAL_FLASH_OTA_STAGING_SIZE },
    { EXTERNAL_FLASH_OTA_METADATA_START, EXTERNAL_FLASH_OTA_METADATA_SIZE },
    { EXTERNAL_FLASH_CONFIG_A_START, EXTERNAL_FLASH_CONFIG_A_SIZE },
    { EXTERNAL_FLASH_CONFIG_B_START, EXTERNAL_FLASH_CONFIG_B_SIZE },
    { EXTERNAL_FLASH_CRASH_START, EXTERNAL_FLASH_CRASH_SIZE },
    { EXTERNAL_FLASH_ALARM_LOG_START, EXTERNAL_FLASH_ALARM_LOG_SIZE },
    { EXTERNAL_FLASH_RUNTIME_LOG_START, EXTERNAL_FLASH_RUNTIME_LOG_SIZE },
    { EXTERNAL_FLASH_RESERVED_START, EXTERNAL_FLASH_RESERVED_SIZE }
};

const external_flash_partition_t *external_flash_partition_get(
    external_flash_partition_id_t id)
{
    return (unsigned int)id < EXTERNAL_FLASH_PARTITION_COUNT
        ? &partitions[(unsigned int)id] : 0;
}

status_t external_flash_layout_validate(void)
{
    uint32_t expected_start = 0u;
    unsigned int i;

    for (i = 0u; i < EXTERNAL_FLASH_PARTITION_COUNT; ++i) {
        const external_flash_partition_t *partition = &partitions[i];

        if (partition->start != expected_start || partition->size == 0u ||
            (partition->start % EXTERNAL_FLASH_SECTOR_SIZE) != 0u ||
            (partition->size % EXTERNAL_FLASH_SECTOR_SIZE) != 0u ||
            partition->size > EXTERNAL_FLASH_TOTAL_SIZE - partition->start) {
            return ERR_INVALID_ARG;
        }
        expected_start = partition->start + partition->size;
    }
    return expected_start == EXTERNAL_FLASH_TOTAL_SIZE
        ? SYS_OK : ERR_INVALID_ARG;
}
