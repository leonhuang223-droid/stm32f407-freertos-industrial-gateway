#ifndef PARTITION_TABLE_H
#define PARTITION_TABLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INTERNAL_FLASH_BASE 0x08000000u

#define INTERNAL_FLASH_SIZE (1024u * 1024u)
#define INTERNAL_FLASH_END (INTERNAL_FLASH_BASE + INTERNAL_FLASH_SIZE)

#define PARTITION_BOOTLOADER_START 0x08000000u
#define PARTITION_BOOTLOADER_SIZE (64u * 1024u)
#define PARTITION_METADATA_A_START 0x08010000u
#define PARTITION_METADATA_A_SIZE (64u * 1024u)
#define PARTITION_APP_A_START 0x08020000u
#define PARTITION_APP_A_SIZE (384u * 1024u)
#define PARTITION_APP_B_START 0x08080000u
#define PARTITION_APP_B_SIZE (384u * 1024u)

/* STM32F407 sectors 7 and 10 are independent descriptor erase units. */
#define PARTITION_APP_IMAGE_SIZE (256u * 1024u)
#define PARTITION_APP_DESCRIPTOR_SIZE (128u * 1024u)
#define PARTITION_APP_A_DESCRIPTOR_START 0x08060000u
#define PARTITION_APP_B_DESCRIPTOR_START 0x080C0000u

#define PARTITION_METADATA_B_START 0x080E0000u
#define PARTITION_METADATA_B_SIZE (128u * 1024u)
#define PARTITION_RESERVED_START INTERNAL_FLASH_END
#define PARTITION_RESERVED_SIZE 0u

/**
 * @brief Logical application slot identifiers shared by Bootloader, App and
 * tools.
 */
typedef enum { SLOT_A = 0, SLOT_B = 1, SLOT_NONE = 0xFF } app_slot_t;

/**
 * @brief Persistent boot state stored in Boot Metadata.
 */
typedef enum {
    BOOT_STATE_NORMAL = 0,
    BOOT_STATE_PENDING,
    BOOT_STATE_TRIAL,
    BOOT_STATE_CONFIRMED,
    BOOT_STATE_ROLLBACK,
    BOOT_STATE_MAINTENANCE
} boot_state_t;

/**
 * @brief Internal Flash partition descriptor.
 *
 * App image partitions describe only the executable body area. Descriptor
 * partitions describe the final 128 KB erase sector in each physical App slot.
 */
typedef struct {
    uint32_t start;
    uint32_t size;
    app_slot_t slot;
} partition_t;

/** @brief Return the immutable Bootloader partition descriptor. */
const partition_t *partition_get_bootloader(void);

/** @brief Return the full physical App slot descriptor for SLOT_A or SLOT_B. */
const partition_t *partition_get_slot(app_slot_t slot);

/** @brief Return the executable body partition for an App slot. */
const partition_t *partition_get_slot_image(app_slot_t slot);

/** @brief Return the 128 KB descriptor erase sector for an App slot. */
const partition_t *partition_get_slot_descriptor(app_slot_t slot);

/** @brief Return the Metadata A/B Flash partition for a copy slot. */
const partition_t *partition_get_metadata(app_slot_t slot);

#ifdef __cplusplus
}
#endif

#endif
