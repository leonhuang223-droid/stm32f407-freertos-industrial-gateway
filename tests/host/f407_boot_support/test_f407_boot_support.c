#include "boot_jump.h"
#include "f407_flash.h"
#include "w25q_boot.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define EXPECT_TRUE(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        failures++; \
    } \
} while (0)

#define EXPECT_EQ(expected, actual) do { \
    int expected_value = (int)(expected); \
    int actual_value = (int)(actual); \
    if (expected_value != actual_value) { \
        printf("FAIL %s:%d: expected %d actual %d\n", \
               __FILE__, __LINE__, expected_value, actual_value); \
        failures++; \
    } \
} while (0)

#define EXPECT_U32(expected, actual) do { \
    uint32_t expected_value = (uint32_t)(expected); \
    uint32_t actual_value = (uint32_t)(actual); \
    if (expected_value != actual_value) { \
        printf("FAIL %s:%d: expected 0x%08lx actual 0x%08lx\n", \
               __FILE__, __LINE__, (unsigned long)expected_value, \
               (unsigned long)actual_value); \
        failures++; \
    } \
} while (0)

typedef struct {
    uint8_t bytes[INTERNAL_FLASH_SIZE];
    uint8_t erased[16];
    size_t erase_count;
    size_t program_count;
    size_t unlock_count;
    size_t lock_count;
} fake_flash_t;

static fake_flash_t flash_fixture;

static status_t flash_unlock(void *context)
{
    fake_flash_t *flash = context;
    flash->unlock_count++;
    return SYS_OK;
}

static status_t flash_lock(void *context)
{
    fake_flash_t *flash = context;
    flash->lock_count++;
    return SYS_OK;
}

static status_t flash_erase(void *context, uint8_t sector_index)
{
    fake_flash_t *flash = context;
    const f407_flash_sector_t *sector =
        f407_flash_sector_get(sector_index);

    if (sector == 0 || flash->erase_count >= sizeof(flash->erased)) {
        return ERR_FLASH_ERASE;
    }
    flash->erased[flash->erase_count++] = sector_index;
    memset(&flash->bytes[sector->start - INTERNAL_FLASH_BASE],
           0xFF, sector->size);
    return SYS_OK;
}

static status_t flash_program(void *context, uint32_t address,
                              uint32_t value)
{
    fake_flash_t *flash = context;

    if ((address & 3u) != 0u || address < INTERNAL_FLASH_BASE ||
        address > INTERNAL_FLASH_END - sizeof(value)) {
        return ERR_FLASH_WRITE;
    }
    memcpy(&flash->bytes[address - INTERNAL_FLASH_BASE],
           &value, sizeof(value));
    flash->program_count++;
    return SYS_OK;
}

static status_t flash_read(void *context, uint32_t address,
                           uint8_t *buffer, size_t length)
{
    fake_flash_t *flash = context;

    if (buffer == 0 || address < INTERNAL_FLASH_BASE ||
        length > (size_t)(INTERNAL_FLASH_END - address)) {
        return ERR_FLASH_VERIFY;
    }
    memcpy(buffer, &flash->bytes[address - INTERNAL_FLASH_BASE], length);
    return SYS_OK;
}

static f407_flash_t make_flash(void)
{
    f407_flash_t flash;
    f407_flash_port_t port;

    memset(&flash_fixture, 0, sizeof(flash_fixture));
    memset(flash_fixture.bytes, 0xFF, sizeof(flash_fixture.bytes));
    memset(&port, 0, sizeof(port));
    port.unlock = flash_unlock;
    port.lock = flash_lock;
    port.erase_sector = flash_erase;
    port.program_word = flash_program;
    port.read = flash_read;
    port.context = &flash_fixture;
    EXPECT_EQ(SYS_OK, f407_flash_init(&flash, &port));
    return flash;
}

static void test_sector_map_and_protected_erase(void)
{
    f407_flash_t flash = make_flash();
    const f407_flash_sector_t *sector;

    sector = f407_flash_sector_for_address(0x0800FFFFu);
    EXPECT_TRUE(sector != 0);
    EXPECT_EQ(3, sector->index);
    sector = f407_flash_sector_for_address(PARTITION_METADATA_A_START);
    EXPECT_TRUE(sector != 0);
    EXPECT_EQ(4, sector->index);
    sector = f407_flash_sector_for_address(PARTITION_METADATA_B_START);
    EXPECT_TRUE(sector != 0);
    EXPECT_EQ(11, sector->index);

    EXPECT_EQ(SYS_OK, f407_flash_erase_inactive(
        &flash, PARTITION_APP_B_START, PARTITION_APP_IMAGE_SIZE, SLOT_A));
    EXPECT_EQ(2, flash_fixture.erase_count);
    EXPECT_EQ(8, flash_fixture.erased[0]);
    EXPECT_EQ(9, flash_fixture.erased[1]);

    EXPECT_EQ(ERR_FLASH_ERASE, f407_flash_erase_inactive(
        &flash, PARTITION_APP_A_START, PARTITION_APP_IMAGE_SIZE, SLOT_A));
    EXPECT_EQ(ERR_INVALID_ARG, f407_flash_erase_inactive(
        &flash, PARTITION_APP_B_START + 1u,
        PARTITION_APP_IMAGE_SIZE, SLOT_A));
}

static void test_metadata_descriptor_and_word_program(void)
{
    f407_flash_t flash = make_flash();
    const uint8_t payload[5] = { 1u, 2u, 3u, 4u, 5u };
    uint8_t readback[8];

    EXPECT_EQ(SYS_OK, f407_flash_erase_metadata(&flash, SLOT_A));
    EXPECT_EQ(4, flash_fixture.erased[0]);
    EXPECT_EQ(SYS_OK, f407_flash_erase_metadata(&flash, SLOT_B));
    EXPECT_EQ(11, flash_fixture.erased[1]);
    EXPECT_EQ(SYS_OK, f407_flash_erase_descriptor(&flash, SLOT_B));
    EXPECT_EQ(10, flash_fixture.erased[2]);

    EXPECT_EQ(SYS_OK, f407_flash_program_inactive(
        &flash, PARTITION_APP_B_START, payload, sizeof(payload), SLOT_A));
    EXPECT_EQ(2, flash_fixture.program_count);
    EXPECT_EQ(SYS_OK, f407_flash_read(
        &flash, PARTITION_APP_B_START, readback, sizeof(readback)));
    EXPECT_TRUE(memcmp(payload, readback, sizeof(payload)) == 0);
    EXPECT_EQ(0xFF, readback[5]);
    EXPECT_EQ(0xFF, readback[6]);
    EXPECT_EQ(0xFF, readback[7]);
    EXPECT_EQ(ERR_FLASH_WRITE, f407_flash_program_inactive(
        &flash, PARTITION_APP_A_START, payload, sizeof(payload), SLOT_A));
}

typedef struct {
    uint32_t jedec_id;
    uint32_t read_address;
    uint8_t command;
    unsigned int select_count;
    unsigned int deselect_count;
    unsigned int delay_ms;
    status_t transmit_result;
} fake_w25_t;

static status_t w25_select(void *context, int active)
{
    fake_w25_t *fake = context;

    if (active != 0) {
        fake->select_count++;
    } else {
        fake->deselect_count++;
    }
    return SYS_OK;
}

static status_t w25_transmit(void *context, const uint8_t *data,
                             size_t length)
{
    fake_w25_t *fake = context;

    if (fake->transmit_result != SYS_OK) {
        return fake->transmit_result;
    }
    if (data == 0 || (length != 1u && length != 4u)) {
        return ERR_INVALID_ARG;
    }
    fake->command = data[0];
    if (length == 4u) {
        fake->read_address = ((uint32_t)data[1] << 16u) |
                             ((uint32_t)data[2] << 8u) |
                             (uint32_t)data[3];
    }
    return SYS_OK;
}

static status_t w25_receive(void *context, uint8_t *data, size_t length)
{
    fake_w25_t *fake = context;
    size_t i;

    if (fake->command == 0x9Fu && length == 3u) {
        data[0] = (uint8_t)(fake->jedec_id >> 16u);
        data[1] = (uint8_t)(fake->jedec_id >> 8u);
        data[2] = (uint8_t)fake->jedec_id;
        return SYS_OK;
    }
    if (fake->command != 0x03u) {
        return ERR_INVALID_ARG;
    }
    for (i = 0u; i < length; ++i) {
        data[i] = (uint8_t)(fake->read_address + (uint32_t)i);
    }
    return SYS_OK;
}

static void w25_delay(void *context, uint32_t delay_ms)
{
    ((fake_w25_t *)context)->delay_ms += delay_ms;
}

static w25q_boot_port_t make_w25_port(fake_w25_t *fake)
{
    w25q_boot_port_t port;

    memset(&port, 0, sizeof(port));
    port.select = w25_select;
    port.transmit = w25_transmit;
    port.receive = w25_receive;
    port.delay_ms = w25_delay;
    port.context = fake;
    return port;
}

static void test_w25_jedec_and_bounded_read(void)
{
    fake_w25_t fake;
    w25q_boot_t device;
    w25q_boot_config_t config;
    w25q_boot_port_t port;
    uint8_t bytes[4];

    memset(&fake, 0, sizeof(fake));
    fake.jedec_id = W25Q128_JEDEC_ID;
    fake.transmit_result = SYS_OK;
    config.expected_jedec_id = W25Q128_JEDEC_ID;
    config.total_size = W25Q128_TOTAL_SIZE;
    port = make_w25_port(&fake);

    EXPECT_EQ(SYS_OK, w25q_boot_init(&device, &config, &port));
    EXPECT_U32(W25Q128_JEDEC_ID,
               w25q_boot_detected_jedec_id(&device));
    EXPECT_EQ(1, fake.delay_ms);
    EXPECT_EQ(2, fake.select_count);
    EXPECT_EQ(2, fake.deselect_count);

    EXPECT_EQ(SYS_OK, w25q_boot_read(&device, 0x001234u,
                                     bytes, sizeof(bytes)));
    EXPECT_U32(0x001234u, fake.read_address);
    EXPECT_EQ(0x34, bytes[0]);
    EXPECT_EQ(0x37, bytes[3]);
    EXPECT_EQ(ERR_INVALID_ARG, w25q_boot_read(
        &device, W25Q128_TOTAL_SIZE - 1u, bytes, sizeof(bytes)));

    memset(&fake, 0, sizeof(fake));
    fake.jedec_id = 0x123456u;
    fake.transmit_result = SYS_OK;
    port = make_w25_port(&fake);
    EXPECT_EQ(ERR_UNSUPPORTED,
              w25q_boot_init(&device, &config, &port));
}

static void test_f407_vector_bounds(void)
{
    EXPECT_U32(0x20020000u, BOOT_SRAM_END);
    EXPECT_EQ(SYS_OK, boot_jump_validate_vectors(
        SLOT_A, BOOT_SRAM_END, PARTITION_APP_A_START + 1u));
    EXPECT_EQ(SYS_OK, boot_jump_validate_vectors(
        SLOT_B, BOOT_SRAM_END, PARTITION_APP_B_START + 1u));
    EXPECT_EQ(ERR_IMAGE_INVALID, boot_jump_validate_vectors(
        SLOT_A, BOOT_SRAM_END + 8u, PARTITION_APP_A_START + 1u));
    EXPECT_EQ(ERR_IMAGE_INVALID, boot_jump_validate_vectors(
        SLOT_A, BOOT_SRAM_END, PARTITION_APP_A_START));
}

int main(void)
{
    test_sector_map_and_protected_erase();
    test_metadata_descriptor_and_word_program();
    test_w25_jedec_and_bounded_read();
    test_f407_vector_bounds();

    if (failures != 0) {
        printf("%d F407 boot support checks failed\n", failures);
        return 1;
    }
    printf("F407 boot support host checks passed\n");
    return 0;
}
