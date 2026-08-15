#include "partition_table.h"

static const partition_t bootloader_partition = {
    PARTITION_BOOTLOADER_START,
    PARTITION_BOOTLOADER_SIZE,
    SLOT_NONE
};

static const partition_t app_a_partition = {
    PARTITION_APP_A_START,
    PARTITION_APP_A_SIZE,
    SLOT_A
};

static const partition_t app_b_partition = {
    PARTITION_APP_B_START,
    PARTITION_APP_B_SIZE,
    SLOT_B
};

static const partition_t app_a_image_partition = {
    PARTITION_APP_A_START,
    PARTITION_APP_IMAGE_SIZE,
    SLOT_A
};

static const partition_t app_b_image_partition = {
    PARTITION_APP_B_START,
    PARTITION_APP_IMAGE_SIZE,
    SLOT_B
};

static const partition_t app_a_descriptor_partition = {
    PARTITION_APP_A_DESCRIPTOR_START,
    PARTITION_APP_DESCRIPTOR_SIZE,
    SLOT_A
};

static const partition_t app_b_descriptor_partition = {
    PARTITION_APP_B_DESCRIPTOR_START,
    PARTITION_APP_DESCRIPTOR_SIZE,
    SLOT_B
};

static const partition_t metadata_a_partition = {
    PARTITION_METADATA_A_START,
    PARTITION_METADATA_A_SIZE,
    SLOT_A
};

static const partition_t metadata_b_partition = {
    PARTITION_METADATA_B_START,
    PARTITION_METADATA_B_SIZE,
    SLOT_B
};

const partition_t *partition_get_bootloader(void)
{
    return &bootloader_partition;
}

const partition_t *partition_get_slot(app_slot_t slot)
{
    switch (slot) {
    case SLOT_A:
        return &app_a_partition;
    case SLOT_B:
        return &app_b_partition;
    default:
        return 0;
    }
}

const partition_t *partition_get_slot_image(app_slot_t slot)
{
    switch (slot) {
    case SLOT_A:
        return &app_a_image_partition;
    case SLOT_B:
        return &app_b_image_partition;
    default:
        return 0;
    }
}

const partition_t *partition_get_slot_descriptor(app_slot_t slot)
{
    switch (slot) {
    case SLOT_A:
        return &app_a_descriptor_partition;
    case SLOT_B:
        return &app_b_descriptor_partition;
    default:
        return 0;
    }
}

const partition_t *partition_get_metadata(app_slot_t slot)
{
    switch (slot) {
    case SLOT_A:
        return &metadata_a_partition;
    case SLOT_B:
        return &metadata_b_partition;
    default:
        return 0;
    }
}
