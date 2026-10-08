#ifndef BOOTLOADER_CORE_H
#define BOOTLOADER_CORE_H

#include "boot_metadata.h"
#include "error_code.h"
#include "partition_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Last major Bootloader step reached by the decision engine.
 */
typedef enum {
    BOOTLOADER_STAGE_RESET = 0,
    BOOTLOADER_STAGE_INIT,
    BOOTLOADER_STAGE_LOAD_METADATA,
    BOOTLOADER_STAGE_RECOVERY_SCAN,
    BOOTLOADER_STAGE_CHECK_PENDING,
    BOOTLOADER_STAGE_VERIFY_STAGING,
    BOOTLOADER_STAGE_PROGRAM_INACTIVE,
    BOOTLOADER_STAGE_SET_TRIAL,
    BOOTLOADER_STAGE_TRIAL_CHECK,
    BOOTLOADER_STAGE_VALIDATE_SLOT,
    BOOTLOADER_STAGE_JUMP_APP,
    BOOTLOADER_STAGE_MAINTENANCE
} bootloader_stage_t;

/**
 * @brief Action selected by the Bootloader state machine.
 */
typedef enum {
    BOOTLOADER_ACTION_NONE = 0,
    BOOTLOADER_ACTION_JUMP_SLOT,
    BOOTLOADER_ACTION_MAINTENANCE
} bootloader_action_t;

/**
 * @brief Observable result of one Bootloader decision pass.
 */
typedef struct {
    bootloader_action_t action; /**< Jump, maintenance, or no action. */
    bootloader_stage_t stage;   /**< Stage where the decision finished. */
    status_t status;            /**< Final status or failure reason. */
    app_slot_t selected_slot;   /**< Slot selected for jump when applicable. */
    boot_metadata_t metadata;   /**< Metadata snapshot used or committed. */
} bootloader_decision_t;

/**
 * @brief Hardware and persistence callbacks for the HAL-free Bootloader core.
 *
 * All callbacks run in bare-metal Bootloader context. They may block with
 * bounded hardware timeouts but must not depend on FreeRTOS or Application
 * services.
 */
typedef struct {
    status_t (*minimal_init)(
        void *context); /**< Initialize clock/GPIO/Flash/SPI only. */
    status_t (*get_reset_reason)(void *context,
                                 boot_reset_reason_t *out_reason);
    status_t (*validate_slot)(void *context,
                              app_slot_t slot); /**< Verify installed image. */
    status_t (*confirm_slot)(
        void *context, app_slot_t slot); /**< Commit descriptor confirmed. */
    status_t (*validate_staging)(void *context,
                                 const boot_metadata_t *metadata);
    status_t (*program_inactive_slot)(void *context,
                                      const boot_metadata_t *metadata);
    status_t (*jump_to_slot)(
        void *context, app_slot_t slot); /**< Does not return on hardware. */
    status_t (*enter_maintenance)(void *context, status_t reason);
    status_t (*scan_recovery)(void *context,
                              boot_meta_recovery_scan_t *out_scan);
    boot_meta_store_t metadata_store; /**< Internal Flash Metadata A/B store. */
    void *context; /**< Opaque platform context passed to callbacks. */
} bootloader_port_t;

/** @brief Bind the Bootloader core to its platform port. */
status_t bootloader_init(const bootloader_port_t *port);

/** @brief Run the A/B decision logic without performing the final action. */
status_t bootloader_select_slot(bootloader_decision_t *out_decision);

/** @brief Program the inactive slot from the already validated staging package.
 */
status_t bootloader_program_inactive_slot(const boot_metadata_t *metadata);

/** @brief Validate one installed slot through the platform port. */
status_t bootloader_validate_slot(app_slot_t slot);

/** @brief Jump to a validated slot through the platform port. */
status_t bootloader_jump_to_slot(app_slot_t slot);

/** @brief Enter bounded maintenance handling through the platform port. */
status_t bootloader_enter_maintenance(status_t reason);

/** @brief Execute the full Bootloader flow, including final jump/maintenance
 * action. */
status_t bootloader_execute(int force_maintenance,
                            bootloader_decision_t *out_decision);

#ifdef __cplusplus
}
#endif

#endif
