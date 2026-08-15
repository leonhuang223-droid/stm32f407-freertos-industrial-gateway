#ifndef IMAGE_DESCRIPTOR_H
#define IMAGE_DESCRIPTOR_H

#include "boot_metadata.h"
#include "image_verify.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Storage callbacks for descriptor page reads and in-place state updates. */
typedef struct {
    /** Read bytes from an absolute flash address. */
    status_t (*read)(void *context, uint32_t address,
                     uint8_t *buffer, size_t length);
    /** Erase the descriptor flash page that contains address..address+length. */
    status_t (*erase)(void *context, uint32_t address, size_t length);
    /** Write descriptor bytes to an absolute flash address. */
    status_t (*write)(void *context, uint32_t address,
                      const uint8_t *data, size_t length);
    void *context; /**< Caller-owned storage implementation context. */
} image_descriptor_store_t;

/** Per-slot status produced while scanning descriptor pages for recovery. */
typedef struct {
    status_t app_a_status; /**< Validation status for App A descriptor/body. */
    status_t app_b_status; /**< Validation status for App B descriptor/body. */
} image_descriptor_scan_status_t;

/**
 * @brief Verify an installed application using descriptor and body readers.
 * @param slot Expected application slot.
 * @param descriptor_read_fn Reader for the slot-tail descriptor page.
 * @param descriptor_context Context passed to descriptor_read_fn.
 * @param image_read_fn Reader for the slot-start raw application body.
 * @param image_context Context passed to image_read_fn.
 * @param scratch Temporary buffer used for streaming hash/CRC checks.
 * @param scratch_size Size of scratch in bytes.
 * @param out_header Optional verified header output.
 * @return SYS_OK when descriptor, slot binding, vector table, CRC, and SHA pass.
 */
status_t image_descriptor_verify_installed(app_slot_t slot,
                                           image_verify_read_fn descriptor_read_fn,
                                           void *descriptor_context,
                                           image_verify_read_fn image_read_fn,
                                           void *image_context,
                                           uint8_t *scratch,
                                           size_t scratch_size,
                                           image_header_t *out_header);

/**
 * @brief Scan both descriptor pages and build boot metadata recovery hints.
 *
 * A completed scan returns SYS_OK even when a slot is unreadable or invalid.
 * The exact per-slot result is returned in out_status and that slot is mapped
 * to image_valid=0 while the other slot is still scanned.
 *
 * @param store Descriptor store callbacks.
 * @param scratch Temporary streaming buffer.
 * @param scratch_size Size of scratch in bytes.
 * @param out_scan Recovered descriptor state for boot metadata repair.
 * @param out_status Optional per-slot validation status.
 * @return SYS_OK when the scan itself completed.
 */
status_t image_descriptor_scan_recovery(
    const image_descriptor_store_t *store,
    uint8_t *scratch,
    size_t scratch_size,
    boot_meta_recovery_scan_t *out_scan,
    image_descriptor_scan_status_t *out_status);

/**
 * @brief Rewrite a candidate descriptor as confirmed after app self-test.
 * @param store Descriptor page storage callbacks.
 * @param slot Slot whose descriptor should be confirmed.
 * @param scratch Temporary buffer for descriptor verification and rewrite.
 * @param scratch_size Size of scratch in bytes.
 * @param out_header Optional confirmed header output.
 * @return SYS_OK when the candidate descriptor is valid and rewritten.
 */
status_t image_descriptor_confirm_candidate(
    const image_descriptor_store_t *store,
    app_slot_t slot,
    uint8_t *scratch,
    size_t scratch_size,
    image_header_t *out_header);

#ifdef __cplusplus
}
#endif

#endif
