#ifndef IMAGE_VERIFY_H
#define IMAGE_VERIFY_H

#include "error_code.h"
#include "image_header.h"
#include "partition_table.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Offset-based image reader used by shared verification routines.
 *
 * Reader callbacks must fill exactly length bytes into buffer when returning
 * SYS_OK. Short reads must be reported as an error status by the callback.
 *
 * @param context Caller-owned reader context.
 * @param offset Offset from the selected package/body base.
 * @param buffer Destination buffer.
 * @param length Number of bytes requested.
 * @return SYS_OK only when all requested bytes were read.
 */
typedef status_t (*image_verify_read_fn)(void *context, uint32_t offset,
                                         uint8_t *buffer, size_t length);

/**
 * @brief Validate image header magic, version, slot, sizes, and state fields.
 * @param header Header to validate.
 * @return SYS_OK when the header is structurally valid.
 */
status_t image_verify_header(const image_header_t *header);
/**
 * @brief Check that a header is intended for the expected slot.
 * @param header Header to inspect.
 * @param expected_slot Slot selected by boot metadata or manifest.
 * @return SYS_OK when the slot binding matches.
 */
status_t image_verify_slot_binding(const image_header_t *header, app_slot_t expected_slot);
/**
 * @brief Verify CRC32 over the image body at header->image_offset.
 * @param header Valid image header.
 * @param read_fn Package/body reader.
 * @param context Reader context.
 * @param scratch Temporary buffer for streaming reads.
 * @param scratch_size Size of scratch in bytes.
 * @return SYS_OK when the computed CRC matches the header.
 */
status_t image_verify_crc32(const image_header_t *header, image_verify_read_fn read_fn,
                            void *context, uint8_t *scratch, size_t scratch_size);
/**
 * @brief Verify SHA256 over the image body at header->image_offset.
 * @param header Valid image header.
 * @param read_fn Package/body reader.
 * @param context Reader context.
 * @param scratch Temporary buffer for streaming reads.
 * @param scratch_size Size of scratch in bytes.
 * @return SYS_OK when the computed digest matches the header.
 */
status_t image_verify_sha256(const image_header_t *header, image_verify_read_fn read_fn,
                             void *context, uint8_t *scratch, size_t scratch_size);
/**
 * @brief Verify CRC32 over the image body at an explicit reader offset.
 * @param header Valid image header.
 * @param reader_offset Offset of the raw body in the reader's address space.
 * @param read_fn Body reader.
 * @param context Reader context.
 * @param scratch Temporary buffer for streaming reads.
 * @param scratch_size Size of scratch in bytes.
 * @return SYS_OK when the computed CRC matches the header.
 */
status_t image_verify_crc32_at(const image_header_t *header, uint32_t reader_offset,
                               image_verify_read_fn read_fn, void *context,
                               uint8_t *scratch, size_t scratch_size);
/**
 * @brief Verify SHA256 over the image body at an explicit reader offset.
 * @param header Valid image header.
 * @param reader_offset Offset of the raw body in the reader's address space.
 * @param read_fn Body reader.
 * @param context Reader context.
 * @param scratch Temporary buffer for streaming reads.
 * @param scratch_size Size of scratch in bytes.
 * @return SYS_OK when the computed digest matches the header.
 */
status_t image_verify_sha256_at(const image_header_t *header, uint32_t reader_offset,
                                image_verify_read_fn read_fn, void *context,
                                uint8_t *scratch, size_t scratch_size);
/**
 * @brief Validate initial MSP and Reset_Handler addresses in a vector table.
 * @param header Header whose link address defines the expected slot vector base.
 * @param expected_slot Slot selected by boot policy.
 * @param reader_offset Offset of the vector table in the reader's address space.
 * @param read_fn Body reader.
 * @param context Reader context.
 * @return SYS_OK when the vector table points inside RAM/slot bounds.
 */
status_t image_verify_vector_table_at(const image_header_t *header,
                                      app_slot_t expected_slot,
                                      uint32_t reader_offset,
                                      image_verify_read_fn read_fn,
                                      void *context);

#ifdef __cplusplus
}
#endif

#endif
