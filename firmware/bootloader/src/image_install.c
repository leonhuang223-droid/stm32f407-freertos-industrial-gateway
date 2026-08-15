#include "image_install.h"

#include "image_descriptor.h"
#include "image_verify.h"
#include "partition_table.h"

typedef struct {
    const image_install_port_t *port;
    uint32_t base_address;
} flash_reader_context_t;

static int is_app_slot(app_slot_t slot)
{
    return slot == SLOT_A || slot == SLOT_B;
}

static int port_complete(const image_install_port_t *port)
{
    return port != 0 &&
           port->package_read != 0 &&
           port->flash_erase != 0 &&
           port->flash_write != 0 &&
           port->flash_read != 0;
}

static int partition_end(const partition_t *partition, uint32_t *out_end)
{
    if (partition == 0 || out_end == 0 ||
        partition->size > UINT32_MAX - partition->start) {
        return 0;
    }
    *out_end = partition->start + partition->size;
    return 1;
}

static int layout_is_safe(const partition_t *physical,
                          const partition_t *image,
                          const partition_t *descriptor)
{
    uint32_t physical_end;
    uint32_t image_end;
    uint32_t descriptor_end;

    if (!partition_end(physical, &physical_end) ||
        !partition_end(image, &image_end) ||
        !partition_end(descriptor, &descriptor_end)) {
        return 0;
    }
    return image->start == physical->start &&
           image_end == descriptor->start &&
           descriptor_end == physical_end &&
           (image->start & 1u) == 0u &&
           (descriptor->start & 1u) == 0u &&
           (descriptor->size & 1u) == 0u &&
           (sizeof(image_header_t) & 1u) == 0u;
}

static status_t flash_read_relative(void *context, uint32_t offset,
                                    uint8_t *buffer, size_t length)
{
    flash_reader_context_t *reader = (flash_reader_context_t *)context;

    if (reader == 0 || reader->port == 0 ||
        offset > UINT32_MAX - reader->base_address) {
        return ERR_INVALID_ARG;
    }
    return reader->port->flash_read(reader->port->context,
                                    reader->base_address + offset,
                                    buffer, length);
}

static status_t write_image_body(const image_install_port_t *port,
                                 const image_header_t *header,
                                 const partition_t *image_partition,
                                 uint8_t *scratch,
                                 size_t scratch_size)
{
    uint32_t written = 0u;
    status_t status;

    status = port->flash_erase(port->context, image_partition->start,
                               image_partition->size);
    if (status != SYS_OK) {
        return status;
    }

    while (written < header->image_size) {
        uint32_t remaining = header->image_size - written;
        size_t chunk = scratch_size < (size_t)remaining ?
                       scratch_size : (size_t)remaining;
        uint32_t package_offset;
        uint32_t flash_address;

        if (written > UINT32_MAX - header->image_offset ||
            written > UINT32_MAX - image_partition->start) {
            return ERR_IMAGE_INVALID;
        }
        package_offset = header->image_offset + written;
        flash_address = image_partition->start + written;

        status = port->package_read(port->context, package_offset,
                                    scratch, chunk);
        if (status != SYS_OK) {
            return status;
        }
        status = port->flash_write(port->context, flash_address,
                                   scratch, chunk);
        if (status != SYS_OK) {
            return status;
        }
        written += (uint32_t)chunk;
    }

    return SYS_OK;
}

status_t image_install_package(const image_install_port_t *port,
                               app_slot_t active_slot,
                               app_slot_t target_slot,
                               size_t package_size,
                               uint8_t *scratch,
                               size_t scratch_size,
                               image_header_t *out_header)
{
    const partition_t *physical_partition;
    const partition_t *image_partition;
    const partition_t *descriptor_partition;
    flash_reader_context_t image_reader;
    flash_reader_context_t descriptor_reader;
    image_header_t header;
    status_t status;

    if (!port_complete(port) || scratch == 0 ||
        scratch_size < 2u || (scratch_size & 1u) != 0u) {
        return ERR_INVALID_ARG;
    }
    if (!is_app_slot(active_slot) || !is_app_slot(target_slot) ||
        active_slot == target_slot) {
        return ERR_SLOT_MISMATCH;
    }
    if (package_size < sizeof(image_header_t)) {
        return ERR_IMAGE_INVALID;
    }

    status = port->package_read(port->context, 0u,
                                (uint8_t *)&header, sizeof(header));
    if (status != SYS_OK) {
        return status;
    }
    status = image_header_validate_for_slot(&header, target_slot);
    if (status != SYS_OK) {
        return status;
    }
    if (image_header_get_state(&header) != IMAGE_STATE_CANDIDATE) {
        return ERR_IMAGE_INVALID;
    }
    status = image_header_validate_package_layout(&header, package_size);
    if (status != SYS_OK) {
        return status;
    }
    status = image_verify_vector_table_at(&header, target_slot,
                                          header.image_offset,
                                          port->package_read, port->context);
    if (status != SYS_OK) {
        return status;
    }
    status = image_verify_crc32(&header, port->package_read, port->context,
                                scratch, scratch_size);
    if (status != SYS_OK) {
        return status;
    }
    status = image_verify_sha256(&header, port->package_read, port->context,
                                 scratch, scratch_size);
    if (status != SYS_OK) {
        return status;
    }

    physical_partition = partition_get_slot(target_slot);
    image_partition = partition_get_slot_image(target_slot);
    descriptor_partition = partition_get_slot_descriptor(target_slot);
    if (!layout_is_safe(physical_partition, image_partition, descriptor_partition) ||
        header.image_size > image_partition->size) {
        return ERR_IMAGE_INVALID;
    }

    status = write_image_body(port, &header, image_partition,
                              scratch, scratch_size);
    if (status != SYS_OK) {
        return status;
    }

    image_reader.port = port;
    image_reader.base_address = image_partition->start;
    status = image_verify_vector_table_at(&header, target_slot, 0u,
                                          flash_read_relative, &image_reader);
    if (status != SYS_OK) {
        return status;
    }
    status = image_verify_crc32_at(&header, 0u,
                                   flash_read_relative, &image_reader,
                                   scratch, scratch_size);
    if (status != SYS_OK) {
        return status;
    }
    status = image_verify_sha256_at(&header, 0u,
                                    flash_read_relative, &image_reader,
                                    scratch, scratch_size);
    if (status != SYS_OK) {
        return status;
    }

    status = port->flash_erase(port->context, descriptor_partition->start,
                               descriptor_partition->size);
    if (status != SYS_OK) {
        return status;
    }
    status = port->flash_write(port->context, descriptor_partition->start,
                               (const uint8_t *)&header, sizeof(header));
    if (status != SYS_OK) {
        return status;
    }

    descriptor_reader.port = port;
    descriptor_reader.base_address = descriptor_partition->start;
    status = image_descriptor_verify_installed(target_slot,
                                               flash_read_relative,
                                               &descriptor_reader,
                                               flash_read_relative,
                                               &image_reader,
                                               scratch, scratch_size,
                                               out_header);
    if (status != SYS_OK) {
        return status;
    }

    return SYS_OK;
}
