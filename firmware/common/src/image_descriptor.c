#include "image_descriptor.h"

#include <string.h>

typedef struct {
    const image_descriptor_store_t *store;
    uint32_t base_address;
} descriptor_reader_context_t;

static int is_app_slot(app_slot_t slot)
{
    return slot == SLOT_A || slot == SLOT_B;
}

static status_t descriptor_read_relative(void *context,
                                         uint32_t offset,
                                         uint8_t *buffer,
                                         size_t length)
{
    descriptor_reader_context_t *reader =
        (descriptor_reader_context_t *)context;

    if (reader == 0 || reader->store == 0 || reader->store->read == 0 ||
        offset > UINT32_MAX - reader->base_address) {
        return ERR_INVALID_ARG;
    }
    return reader->store->read(
        reader->store->context, reader->base_address + offset, buffer, length);
}

status_t
image_descriptor_verify_installed(app_slot_t slot,
                                  const image_descriptor_check_t *parameters)
{
    if (parameters == 0) {
        return ERR_INVALID_ARG;
    }
    image_verify_read_fn descriptor_read_fn = parameters->descriptor_read_fn;
    void *descriptor_context = parameters->descriptor_context;
    image_verify_read_fn image_read_fn = parameters->image_read_fn;
    void *image_context = parameters->image_context;
    uint8_t *scratch = parameters->scratch;
    size_t scratch_size = parameters->scratch_size;
    image_header_t *out_header = parameters->out_header;

    image_header_t header;
    status_t status;

    if (descriptor_read_fn == 0 || image_read_fn == 0 || scratch == 0 ||
        scratch_size == 0u) {
        return ERR_INVALID_ARG;
    }

    status = descriptor_read_fn(
        descriptor_context, 0u, (uint8_t *)&header, sizeof(header));
    if (status != SYS_OK) {
        return status;
    }

    status = image_header_validate_for_slot(&header, slot);
    if (status != SYS_OK) {
        return status;
    }
    status = image_verify_vector_table_at(
        &header, slot, 0u, image_read_fn, image_context);
    if (status != SYS_OK) {
        return status;
    }
    status = image_verify_crc32_at(
        &header,
        &(const image_body_check_t){
            0u, image_read_fn, image_context, scratch, scratch_size});
    if (status != SYS_OK) {
        return status;
    }
    status = image_verify_sha256_at(
        &header,
        &(const image_body_check_t){
            0u, image_read_fn, image_context, scratch, scratch_size});
    if (status != SYS_OK) {
        return status;
    }

    if (out_header != 0) {
        *out_header = header;
    }
    return SYS_OK;
}

static status_t scan_slot(const image_descriptor_store_t *store,
                          app_slot_t slot,
                          uint8_t *scratch,
                          size_t scratch_size,
                          boot_meta_scan_slot_t *out_slot)
{
    const partition_t *image_partition = partition_get_slot_image(slot);
    const partition_t *descriptor_partition =
        partition_get_slot_descriptor(slot);
    descriptor_reader_context_t descriptor_reader;
    descriptor_reader_context_t image_reader;
    image_header_t header;
    status_t status;

    memset(out_slot, 0, sizeof(*out_slot));
    if (image_partition == 0 || descriptor_partition == 0) {
        return ERR_SLOT_MISMATCH;
    }

    descriptor_reader.store = store;
    descriptor_reader.base_address = descriptor_partition->start;
    image_reader.store = store;
    image_reader.base_address = image_partition->start;
    status = image_descriptor_verify_installed(
        slot,
        &(const image_descriptor_check_t){descriptor_read_relative,
                                          &descriptor_reader,
                                          descriptor_read_relative,
                                          &image_reader,
                                          scratch,
                                          scratch_size,
                                          &header});
    if (status != SYS_OK) {
        return status;
    }

    out_slot->image_valid = 1u;
    out_slot->image_state = image_header_get_state(&header);
    memcpy(out_slot->version, header.app_version, sizeof(out_slot->version));
    return SYS_OK;
}

status_t
image_descriptor_scan_recovery(const image_descriptor_store_t *store,
                               uint8_t *scratch,
                               size_t scratch_size,
                               boot_meta_recovery_scan_t *out_scan,
                               image_descriptor_scan_status_t *out_status)
{
    if (store == 0 || store->read == 0 || scratch == 0 || scratch_size == 0u ||
        out_scan == 0 || out_status == 0) {
        return ERR_INVALID_ARG;
    }

    memset(out_scan, 0, sizeof(*out_scan));
    out_status->app_a_status =
        scan_slot(store, SLOT_A, scratch, scratch_size, &out_scan->app_a);
    out_status->app_b_status =
        scan_slot(store, SLOT_B, scratch, scratch_size, &out_scan->app_b);
    return SYS_OK;
}

static status_t
persist_confirmed_descriptor(const image_descriptor_store_t *store,
                             const partition_t *descriptor_partition,
                             const image_header_t *confirmed,
                             app_slot_t slot,
                             image_header_t *out_header)
{
    image_header_t readback;
    status_t status;

    status = store->erase(store->context,
                          descriptor_partition->start,
                          descriptor_partition->size);
    if (status != SYS_OK) {
        return status;
    }
    status = store->write(store->context,
                          descriptor_partition->start,
                          (const uint8_t *)confirmed,
                          sizeof(*confirmed));
    if (status != SYS_OK) {
        return status;
    }
    status = store->read(store->context,
                         descriptor_partition->start,
                         (uint8_t *)&readback,
                         sizeof(readback));
    if (status != SYS_OK) {
        return status;
    }
    if (memcmp(&readback, confirmed, sizeof(readback)) != 0) {
        return ERR_FLASH_VERIFY;
    }
    status = image_header_validate_for_slot(&readback, slot);
    if (status != SYS_OK ||
        image_header_get_state(&readback) != IMAGE_STATE_CONFIRMED) {
        return ERR_FLASH_VERIFY;
    }

    if (out_header != 0) {
        *out_header = readback;
    }
    return SYS_OK;
}

status_t
image_descriptor_confirm_candidate(const image_descriptor_store_t *store,
                                   app_slot_t slot,
                                   uint8_t *scratch,
                                   size_t scratch_size,
                                   image_header_t *out_header)
{
    const partition_t *image_partition;
    const partition_t *descriptor_partition;
    descriptor_reader_context_t descriptor_reader;
    descriptor_reader_context_t image_reader;
    image_header_t current;
    image_header_t confirmed;
    status_t status;

    if (store == 0 || store->read == 0 || store->erase == 0 ||
        store->write == 0 || !is_app_slot(slot) || scratch == 0 ||
        scratch_size == 0u) {
        return ERR_INVALID_ARG;
    }

    image_partition = partition_get_slot_image(slot);
    descriptor_partition = partition_get_slot_descriptor(slot);
    if (image_partition == 0 || descriptor_partition == 0) {
        return ERR_SLOT_MISMATCH;
    }

    descriptor_reader.store = store;
    descriptor_reader.base_address = descriptor_partition->start;
    image_reader.store = store;
    image_reader.base_address = image_partition->start;
    status = image_descriptor_verify_installed(
        slot,
        &(const image_descriptor_check_t){descriptor_read_relative,
                                          &descriptor_reader,
                                          descriptor_read_relative,
                                          &image_reader,
                                          scratch,
                                          scratch_size,
                                          &current});
    if (status != SYS_OK) {
        return status;
    }
    if (image_header_get_state(&current) != IMAGE_STATE_CANDIDATE) {
        return ERR_IMAGE_INVALID;
    }

    confirmed = current;
    confirmed.image_flags = IMAGE_FLAG_CONFIRMED;
    status = image_header_refresh_crc(&confirmed);
    if (status != SYS_OK) {
        return status;
    }

    return persist_confirmed_descriptor(
        store, descriptor_partition, &confirmed, slot, out_header);
}
