#ifndef BOOT_METADATA_H
#define BOOT_METADATA_H

#include "error_code.h"
#include "image_header.h"
#include "partition_table.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_METADATA_MAGIC 0x424D4554u
#define BOOT_METADATA_VERSION 1u
#define BOOT_METADATA_VERSION_LEN 16u
#define BOOT_METADATA_DEFAULT_MAX_ATTEMPT 1u

/**
 * @brief Reason codes persisted when the Bootloader abandons a trial/pending image.
 */
typedef enum {
    BOOT_ROLLBACK_NONE = 0,
    BOOT_ROLLBACK_TRIAL_NOT_CONFIRMED,
    BOOT_ROLLBACK_ATTEMPT_EXCEEDED,
    BOOT_ROLLBACK_WATCHDOG,
    BOOT_ROLLBACK_HARDFAULT,
    BOOT_ROLLBACK_STAGING_INVALID,
    BOOT_ROLLBACK_WRITE_FAILED,
    BOOT_ROLLBACK_RECOVER_META_LOST
} boot_rollback_reason_t;

/**
 * @brief Reset reason values normalized from RCC flags and retained fault cookies.
 */
typedef enum {
    BOOT_RESET_REASON_UNKNOWN = 0,
    BOOT_RESET_REASON_POWER_ON,
    BOOT_RESET_REASON_SOFTWARE,
    BOOT_RESET_REASON_PIN,
    BOOT_RESET_REASON_BROWNOUT,
    BOOT_RESET_REASON_IWDG,
    BOOT_RESET_REASON_WWDG,
    BOOT_RESET_REASON_HARDFAULT
} boot_reset_reason_t;

/**
 * @brief Persistent A/B boot state stored in two internal Flash copies.
 *
 * The record is written transactionally: a commit writes the non-current copy
 * with a larger sequence number, reads it back, and validates its CRC before it
 * becomes visible to the Bootloader decision path.
 */
typedef struct {
    uint32_t metadata_magic;       /**< Fixed BOOT_METADATA_MAGIC marker. */
    uint16_t metadata_version;     /**< Persistent record layout version. */
    uint32_t sequence;             /**< Monotonic copy selector. */
    app_slot_t active_slot;        /**< Slot selected for normal boot. */
    app_slot_t previous_slot;      /**< Last confirmed slot for rollback. */
    app_slot_t pending_slot;       /**< Inactive slot awaiting install/trial. */
    boot_state_t boot_state;       /**< Current A/B boot state. */
    uint8_t boot_attempt;          /**< Trial attempt counter. */
    uint8_t max_boot_attempt;      /**< Maximum allowed trial attempts. */
    uint8_t boot_ok;               /**< App-confirmed health flag. */
    char active_version[BOOT_METADATA_VERSION_LEN];  /**< Active App version. */
    char pending_version[BOOT_METADATA_VERSION_LEN]; /**< Candidate App version. */
    uint32_t last_reset_reason;    /**< boot_reset_reason_t stored as u32. */
    uint32_t rollback_reason;      /**< boot_rollback_reason_t stored as u32. */
    uint32_t staging_image_crc32;  /**< Package CRC expected in W25Q128 staging. */
    uint8_t staging_image_sha256[IMAGE_SHA256_LEN]; /**< Package SHA expected in staging. */
    uint32_t metadata_crc32;       /**< CRC32 over all previous fields. */
} boot_metadata_t;

/**
 * @brief Storage callbacks for Metadata A/B copies.
 */
typedef struct {
    status_t (*read)(void *context, app_slot_t copy_slot, boot_metadata_t *out_metadata);
    status_t (*write)(void *context, app_slot_t copy_slot, const boot_metadata_t *metadata);
    void *context;
} boot_meta_store_t;

/**
 * @brief Descriptor scan result for one App slot during Metadata recovery.
 */
typedef struct {
    uint8_t image_valid;
    image_state_t image_state;
    char version[BOOT_METADATA_VERSION_LEN];
} boot_meta_scan_slot_t;

typedef struct {
    boot_meta_scan_slot_t app_a;
    boot_meta_scan_slot_t app_b;
} boot_meta_recovery_scan_t;

/** @brief Initialize a valid NORMAL metadata record for one confirmed slot. */
status_t boot_meta_init_default(boot_metadata_t *metadata, app_slot_t active_slot,
                                const char *active_version);

/** @brief Validate fields and CRC of one metadata record. */
status_t boot_meta_validate(const boot_metadata_t *metadata);

/** @brief Recompute metadata_crc32 in place. */
status_t boot_meta_refresh_crc(boot_metadata_t *metadata);

/** @brief Load the newest valid Metadata copy from storage. */
status_t boot_meta_load(const boot_meta_store_t *store, boot_metadata_t *out_metadata,
                        app_slot_t *out_copy_slot);

/** @brief Transactionally commit a desired record to the inactive copy. */
status_t boot_meta_commit(const boot_meta_store_t *store, const boot_metadata_t *current,
                          app_slot_t current_copy_slot, const boot_metadata_t *desired,
                          boot_metadata_t *out_committed, app_slot_t *out_copy_slot);

/** @brief Request that the Bootloader install a staged image into pending_slot. */
status_t boot_meta_request_staging(const boot_metadata_t *current, app_slot_t pending_slot,
                                   const char *pending_version, uint32_t staging_crc32,
                                   const uint8_t staging_sha256[IMAGE_SHA256_LEN],
                                   boot_metadata_t *out_metadata);

/** @brief Convert a valid pending request into a trial boot state. */
status_t boot_meta_request_trial(const boot_metadata_t *current, boot_metadata_t *out_metadata);

/* BOOT_STATE_CONFIRMED is transient; boot-ok confirmation persists BOOT_STATE_NORMAL. */
/** @brief Confirm a healthy trial image and promote it to NORMAL active. */
status_t boot_meta_confirm_boot_ok(const boot_metadata_t *current, boot_metadata_t *out_metadata);

/** @brief Record rollback and restore the previous confirmed slot. */
status_t boot_meta_mark_rollback(const boot_metadata_t *current, uint32_t rollback_reason,
                                 boot_metadata_t *out_metadata);

/** @brief Rebuild Metadata from installed slot descriptors when both copies are lost. */
status_t boot_meta_recover_by_scan(const boot_meta_recovery_scan_t *scan,
                                   boot_metadata_t *out_metadata);

#ifdef __cplusplus
}
#endif

#endif
