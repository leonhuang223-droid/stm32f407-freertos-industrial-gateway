/** @file test_image_install.c
 * @author 兆鸣嵌入式
 * Verify inactive-slot protection and descriptor commit ordering.
 */
#include "crc32.h"
#include "image_install.h"
#include "platform_constants.h"
#include "sha256.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define BODY_SIZE 32u

typedef struct {
    uint8_t package[sizeof(image_header_t) + BODY_SIZE];
    uint8_t flash[PARTITION_APP_B_SIZE];
    uint32_t erases;
    uint32_t body_writes;
    uint32_t descriptor_writes;
    uint8_t fail_body_write;
} fake_install_t;

static fake_install_t fake;

static status_t
read_package(void *context, uint32_t offset, uint8_t *buffer, size_t length)
{
    const fake_install_t *device = context;
    assert(offset <= sizeof(device->package));
    assert(length <= sizeof(device->package) - offset);
    memcpy(buffer, &device->package[offset], length);
    return SYS_OK;
}

static size_t flash_offset(uint32_t address, size_t length)
{
    assert(address >= PARTITION_APP_B_START);
    assert(address <= PARTITION_APP_B_START + PARTITION_APP_B_SIZE);
    assert(length <= PARTITION_APP_B_SIZE - (address - PARTITION_APP_B_START));
    return address - PARTITION_APP_B_START;
}

static status_t erase_flash(void *context, uint32_t address, size_t length)
{
    fake_install_t *device = context;
    size_t offset = flash_offset(address, length);
    if (address == PARTITION_APP_B_DESCRIPTOR_START) {
        assert(device->body_writes != 0u);
    }
    memset(&device->flash[offset], 0xff, length);
    device->erases++;
    return SYS_OK;
}

static status_t
write_flash(void *context, uint32_t address, const uint8_t *data, size_t length)
{
    fake_install_t *device = context;
    size_t offset = flash_offset(address, length);
    if (address == PARTITION_APP_B_DESCRIPTOR_START) {
        assert(device->body_writes != 0u && device->erases == 2u);
        assert(memcmp(device->flash,
                      &device->package[sizeof(image_header_t)],
                      BODY_SIZE) == 0);
        device->descriptor_writes++;
    } else {
        if (device->fail_body_write != 0u) {
            return ERR_IO;
        }
        device->body_writes++;
    }
    memcpy(&device->flash[offset], data, length);
    return SYS_OK;
}

static status_t
read_flash(void *context, uint32_t address, uint8_t *buffer, size_t length)
{
    const fake_install_t *device = context;
    memcpy(buffer, &device->flash[flash_offset(address, length)], length);
    return SYS_OK;
}

static void make_package(void)
{
    image_header_t header = {0};
    const uint32_t vectors[2] = {0x20002000u, PARTITION_APP_B_START + 9u};
    uint8_t *body = &fake.package[sizeof(header)];

    memset(&fake, 0, sizeof(fake));
    memcpy(body, vectors, sizeof(vectors));
    header.magic = IMAGE_MAGIC;
    header.header_version = 1u;
    header.header_size = sizeof(header);
    (void)snprintf(
        header.target_id, sizeof(header.target_id), "%s", PROJECT_TARGET_ID);
    (void)snprintf(header.app_version, sizeof(header.app_version), "2.0.0");
    (void)snprintf(header.git_sha, sizeof(header.git_sha), "host-test");
    (void)snprintf(header.min_bootloader_version,
                   sizeof(header.min_bootloader_version),
                   "1.0.0");
    header.target_slot = SLOT_B;
    header.link_address = PARTITION_APP_B_START;
    header.image_offset = sizeof(header);
    header.image_size = BODY_SIZE;
    header.image_flags = IMAGE_FLAG_CANDIDATE;
    assert(crc32_compute(body, BODY_SIZE, &header.image_crc32) == SYS_OK);
    assert(sha256_compute(body, BODY_SIZE, header.image_sha256) == SYS_OK);
    assert(image_header_refresh_crc(&header) == SYS_OK);
    memcpy(fake.package, &header, sizeof(header));
}

int main(void)
{
    uint8_t scratch[16];
    image_header_t installed;
    const image_install_port_t port = {
        read_package, erase_flash, write_flash, read_flash, &fake};
    image_install_request_t request = {SLOT_A,
                                       SLOT_B,
                                       sizeof(fake.package),
                                       scratch,
                                       sizeof(scratch),
                                       &installed};

    make_package();
    fake.package[sizeof(image_header_t) + BODY_SIZE - 1u] ^= 1u;
    assert(image_install_package(&port, &request) == ERR_CRC);
    assert(fake.erases == 0u && fake.descriptor_writes == 0u);

    make_package();
    request.active_slot = SLOT_B;
    assert(image_install_package(&port, &request) == ERR_SLOT_MISMATCH);
    assert(fake.erases == 0u);
    request.active_slot = SLOT_A;
    fake.fail_body_write = 1u;
    assert(image_install_package(&port, &request) == ERR_IO);
    assert(fake.erases == 1u && fake.descriptor_writes == 0u);

    make_package();
    assert(image_install_package(&port, &request) == SYS_OK);
    assert(fake.erases == 2u && fake.descriptor_writes == 1u);
    assert(image_header_get_state(&installed) == IMAGE_STATE_CANDIDATE);
    assert(installed.target_slot == SLOT_B);
    return 0;
}
