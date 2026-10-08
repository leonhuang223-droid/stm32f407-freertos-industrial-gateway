#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#include "error_code.h"
#include "partition_table.h"
#include "platform_constants.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Target SRAM base used to validate an application's initial MSP. */
#define BOOT_SRAM_START PROJECT_SRAM_START
/** Target SRAM size used by the active linker profile. */
#define BOOT_SRAM_SIZE PROJECT_SRAM_SIZE
/** Exclusive SRAM end address. */
#define BOOT_SRAM_END (BOOT_SRAM_START + BOOT_SRAM_SIZE)

/** Platform callbacks required for a controlled jump into an application slot.
 */
typedef struct {
    /** Read absolute internal flash bytes. */
    status_t (*read)(void *context,
                     uint32_t address,
                     uint8_t *buffer,
                     size_t length);
    /** Globally disable interrupts before vector handoff. */
    void (*disable_interrupts)(void *context);
    /** Clear pending NVIC state owned by the bootloader. */
    void (*clear_nvic)(void *context);
    /** Stop the bootloader HAL timebase. */
    void (*stop_tick)(void *context);
    /** Deinitialize peripherals before the app owns them. */
    void (*deinit_peripherals)(void *context);
    /** Move the vector table to the selected application slot. */
    void (*set_vtor)(void *context, uint32_t address);
    /** Load MSP and branch to the application Reset_Handler. */
    void (*set_msp_and_branch)(void *context,
                               uint32_t msp,
                               uint32_t reset_handler);
    void *context; /**< Caller-owned platform context. */
} boot_jump_port_t;

/**
 * @brief Validate MSP and Reset_Handler addresses for a slot vector table.
 * @param slot Slot whose flash range should contain Reset_Handler.
 * @param msp Initial stack pointer value.
 * @param reset_handler Reset_Handler address.
 * @return SYS_OK when vectors are inside SRAM/slot bounds.
 */
status_t boot_jump_validate_vectors(app_slot_t slot,
                                    uint32_t msp,
                                    uint32_t reset_handler);
/**
 * @brief Perform the final bootloader-to-application handoff.
 * @param port Platform jump callbacks.
 * @param slot Slot to boot.
 * @return Only returns on validation or port failure.
 */
status_t boot_jump_to_slot(const boot_jump_port_t *port, app_slot_t slot);

#ifdef __cplusplus
}
#endif

#endif
