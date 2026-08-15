#include "boot_jump.h"

static int port_complete(const boot_jump_port_t *port)
{
    return port != 0 &&
           port->read != 0 &&
           port->disable_interrupts != 0 &&
           port->clear_nvic != 0 &&
           port->stop_tick != 0 &&
           port->deinit_peripherals != 0 &&
           port->set_vtor != 0 &&
           port->set_msp_and_branch != 0;
}

status_t boot_jump_validate_vectors(
    app_slot_t slot, uint32_t msp, uint32_t reset_handler)
{
    const partition_t *image = partition_get_slot_image(slot);
    uint32_t reset_address = reset_handler & ~1u;
    uint32_t image_end;

    if (image == 0) {
        return ERR_SLOT_MISMATCH;
    }
    image_end = image->start + PARTITION_APP_IMAGE_SIZE;
    if (msp <= BOOT_SRAM_START || msp > BOOT_SRAM_END ||
        (msp & 7u) != 0u) {
        return ERR_IMAGE_INVALID;
    }
    if ((reset_handler & 1u) == 0u ||
        reset_address < image->start ||
        reset_address >= image_end) {
        return ERR_IMAGE_INVALID;
    }
    return SYS_OK;
}

status_t boot_jump_to_slot(const boot_jump_port_t *port, app_slot_t slot)
{
    const partition_t *image;
    uint32_t vectors[2];
    status_t status;

    if (!port_complete(port)) {
        return ERR_INVALID_ARG;
    }
    image = partition_get_slot_image(slot);
    if (image == 0) {
        return ERR_SLOT_MISMATCH;
    }
    status = port->read(port->context, image->start,
                        (uint8_t *)vectors, sizeof(vectors));
    if (status != SYS_OK) {
        return status;
    }
    status = boot_jump_validate_vectors(slot, vectors[0], vectors[1]);
    if (status != SYS_OK) {
        return status;
    }

    port->disable_interrupts(port->context);
    port->clear_nvic(port->context);
    port->stop_tick(port->context);
    port->deinit_peripherals(port->context);
    port->set_vtor(port->context, image->start);
    port->set_msp_and_branch(port->context, vectors[0], vectors[1]);
    return ERR_RESET_REQUIRED;
}
