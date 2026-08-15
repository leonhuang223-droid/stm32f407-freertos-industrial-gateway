#ifndef IMAGE_INSTALL_H
#define IMAGE_INSTALL_H

#include "image_header.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Flash/package callbacks used while installing a staged OTA package. */
typedef struct {
    /** Read bytes from the staged package at package-relative offset. */
    status_t (*package_read)(void *context, uint32_t offset,
                             uint8_t *buffer, size_t length);
    /** Erase internal flash range before programming. */
    status_t (*flash_erase)(void *context, uint32_t address, size_t length);
    /** Program internal flash bytes. */
    status_t (*flash_write)(void *context, uint32_t address,
                            const uint8_t *data, size_t length);
    /** Read back internal flash for verification. */
    status_t (*flash_read)(void *context, uint32_t address,
                           uint8_t *buffer, size_t length);
    void *context; /**< Caller-owned platform context. */
} image_install_port_t;

/**
 * @brief Install image_header + raw_app.bin into the inactive application slot.
 *
 * The raw body is written at the target slot base. The image header is written
 * to the slot descriptor page after body verification succeeds.
 *
 * @param port Platform package/flash callbacks.
 * @param active_slot Currently bootable slot, protected from erase/write.
 * @param target_slot Slot selected by boot metadata as pending.
 * @param package_size Total bytes in the staged package.
 * @param scratch Temporary buffer for package streaming.
 * @param scratch_size Size of scratch in bytes.
 * @param out_header Optional installed header output.
 * @return SYS_OK when body, descriptor, and verification all succeed.
 */
status_t image_install_package(const image_install_port_t *port,
                               app_slot_t active_slot,
                               app_slot_t target_slot,
                               size_t package_size,
                               uint8_t *scratch,
                               size_t scratch_size,
                               image_header_t *out_header);

#ifdef __cplusplus
}
#endif

#endif
