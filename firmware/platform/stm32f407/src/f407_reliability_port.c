#include "f407_reliability_port.h"

#include "FreeRTOS.h"
#include "f407_fault_capture.h"
#include "f407_board_config.h"
#include "f407_flash.h"
#include "main.h"
#include "platform_f407.h"
#include "task.h"

#include <stdint.h>
#include <string.h>

#define F407_IWDG_LSI_HZ 32000u
#define F407_IWDG_PRESCALER 256u
#define F407_IWDG_PRESCALER_BITS 6u
#define F407_IWDG_RELOAD_MAX 0x0fffu
#define F407_IWDG_START_KEY 0xccccu
#define F407_IWDG_WRITE_KEY 0x5555u
#define F407_IWDG_REFRESH_KEY 0xaaaau
#define F407_WATCHDOG_TIMEOUT_MS 12000u

#ifndef F407_ENABLE_FAULT_INJECTION
#define F407_ENABLE_FAULT_INJECTION 0
#endif

typedef struct {
    f407_flash_t flash;
    uint32_t watchdog_timeout_ms;
    uint32_t watchdog_last_refresh_ms;
    uint8_t watchdog_started;
    uint8_t watchdog_refresh_blocked;
} f407_reliability_context_t;

static f407_reliability_context_t reliability_context;

static uint32_t monotonic_ms(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return HAL_GetTick();
    }
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static status_t watchdog_start(void *context, uint32_t timeout_ms)
{
    f407_reliability_context_t *reliability = context;
    uint32_t reload;
    uint32_t start_ms;

    if (reliability == 0 || timeout_ms < 16u) {
        return ERR_INVALID_ARG;
    }
    reload = (timeout_ms * (F407_IWDG_LSI_HZ / 1000u)) / F407_IWDG_PRESCALER;
    if (reload == 0u || reload - 1u > F407_IWDG_RELOAD_MAX) {
        return ERR_INVALID_ARG;
    }

    RCC->CSR |= RCC_CSR_LSION;
    start_ms = HAL_GetTick();
    while ((RCC->CSR & RCC_CSR_LSIRDY) == 0u) {
        if (HAL_GetTick() - start_ms > 100u) {
            return ERR_TIMEOUT;
        }
    }

    IWDG->KR = F407_IWDG_WRITE_KEY;
    IWDG->PR = F407_IWDG_PRESCALER_BITS;
    IWDG->RLR = reload - 1u;
    start_ms = HAL_GetTick();
    while (IWDG->SR != 0u) {
        if (HAL_GetTick() - start_ms > 100u) {
            return ERR_TIMEOUT;
        }
    }
    IWDG->KR = F407_IWDG_REFRESH_KEY;
    IWDG->KR = F407_IWDG_START_KEY;
    reliability->watchdog_timeout_ms = timeout_ms;
    reliability->watchdog_last_refresh_ms = monotonic_ms();
    reliability->watchdog_started = 1u;
    return SYS_OK;
}

static status_t watchdog_refresh(void *context)
{
    f407_reliability_context_t *reliability = context;

    if (reliability == 0 || reliability->watchdog_started == 0u ||
        reliability->watchdog_refresh_blocked != 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    IWDG->KR = F407_IWDG_REFRESH_KEY;
    reliability->watchdog_last_refresh_ms = monotonic_ms();
    return SYS_OK;
}

static uint32_t watchdog_remaining_ms(const void *context)
{
    const f407_reliability_context_t *reliability = context;
    uint32_t elapsed;

    if (reliability == 0 || reliability->watchdog_started == 0u) {
        return 0u;
    }
    elapsed = monotonic_ms() - reliability->watchdog_last_refresh_ms;
    return elapsed < reliability->watchdog_timeout_ms
               ? reliability->watchdog_timeout_ms - elapsed
               : 0u;
}

static const watchdog_device_ops_t watchdog_ops = {
    watchdog_start, watchdog_refresh, watchdog_remaining_ms};

static status_t deep_power_enter_stop(void *context,
                                      uint32_t requested_ms,
                                      uint32_t *elapsed_ms,
                                      power_wake_reason_t *wake_reason)
{
    (void)context;
    (void)requested_ms;
    if (elapsed_ms == 0 || wake_reason == 0) {
        return ERR_INVALID_ARG;
    }
    *elapsed_ms = 0u;
    *wake_reason = POWER_WAKE_UNKNOWN;

    /* RTC/EXTI wake and post-STOP clock recovery require board evidence. */
    return ERR_UNSUPPORTED;
}

static status_t deep_power_enter_standby(void *context)
{
    (void)context;
    /* Standby resets the MCU, so no unverified wake source is armed here. */
    return ERR_UNSUPPORTED;
}

static const deep_power_platform_ops_t deep_power_ops = {
    deep_power_enter_stop, deep_power_enter_standby};

static status_t fault_record_load(void *context, fault_record_t *record)
{
    (void)context;
    return f407_fault_record_load(record);
}

static status_t fault_record_clear(void *context)
{
    (void)context;
    return f407_fault_record_clear();
}

static status_t fault_inject(void *context, fault_injection_t injection)
{
#if F407_ENABLE_FAULT_INJECTION
    f407_reliability_context_t *reliability = context;
    status_t status;

    if (reliability == 0) {
        return ERR_INVALID_ARG;
    }
    if (injection == FAULT_INJECTION_HARDFAULT) {
        f407_fault_inject_hardfault();
        return ERR_RESET_REQUIRED;
    }
    if (injection == FAULT_INJECTION_WATCHDOG) {
        status = f407_fault_prepare_watchdog_injection();
        if (status == SYS_OK) {
            reliability->watchdog_refresh_blocked = 1u;
        }
        return status;
    }
    return ERR_INVALID_ARG;
#else
    (void)context;
    (void)injection;
    return ERR_UNSUPPORTED;
#endif
}

static const fault_recorder_ops_t fault_recorder_ops = {
    fault_record_load, fault_record_clear, fault_inject};

static status_t flash_unlock(void *context)
{
    (void)context;
    return HAL_FLASH_Unlock() == HAL_OK ? SYS_OK : ERR_FLASH_WRITE;
}

static status_t flash_lock(void *context)
{
    (void)context;
    return HAL_FLASH_Lock() == HAL_OK ? SYS_OK : ERR_FLASH_WRITE;
}

static status_t flash_erase_sector(void *context, uint8_t sector_index)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = UINT32_MAX;

    (void)context;
    if (sector_index >= F407_FLASH_SECTOR_COUNT) {
        return ERR_INVALID_ARG;
    }
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Banks = FLASH_BANK_1;
    erase.Sector = sector_index;
    erase.NbSectors = 1u;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    return HAL_FLASHEx_Erase(&erase, &sector_error) == HAL_OK &&
                   sector_error == UINT32_MAX
               ? SYS_OK
               : ERR_FLASH_ERASE;
}

static status_t
flash_program_word(void *context, uint32_t address, uint32_t value)
{
    (void)context;
    return HAL_FLASH_Program(
               FLASH_TYPEPROGRAM_WORD, address, (uint64_t)value) == HAL_OK
               ? SYS_OK
               : ERR_FLASH_WRITE;
}

static status_t
flash_read(void *context, uint32_t address, uint8_t *buffer, size_t length)
{
    (void)context;
    if (buffer == 0 || length == 0u || address < INTERNAL_FLASH_BASE ||
        address >= INTERNAL_FLASH_END ||
        length > (size_t)(INTERNAL_FLASH_END - address)) {
        return ERR_INVALID_ARG;
    }
    memcpy(buffer, (const void *)(uintptr_t)address, length);
    return SYS_OK;
}

static status_t
metadata_read(void *context, app_slot_t copy_slot, boot_metadata_t *metadata)
{
    f407_reliability_context_t *reliability = context;
    const partition_t *partition = partition_get_metadata(copy_slot);

    if (reliability == 0 || partition == 0 || metadata == 0) {
        return ERR_INVALID_ARG;
    }
    return f407_flash_read(&reliability->flash,
                           partition->start,
                           (uint8_t *)metadata,
                           sizeof(*metadata));
}

static status_t metadata_write(void *context,
                               app_slot_t copy_slot,
                               const boot_metadata_t *metadata)
{
    f407_reliability_context_t *reliability = context;
    boot_metadata_t readback;
    status_t status;
    status_t lock_status;

    if (reliability == 0 || metadata == 0) {
        return ERR_INVALID_ARG;
    }
    status = boot_meta_validate(metadata);
    if (status != SYS_OK) {
        return status;
    }
    status = f407_flash_unlock(&reliability->flash);
    if (status == SYS_OK) {
        status = f407_flash_erase_metadata(&reliability->flash, copy_slot);
    }
    if (status == SYS_OK) {
        status = f407_flash_program_metadata(&reliability->flash,
                                             copy_slot,
                                             (const uint8_t *)metadata,
                                             sizeof(*metadata));
    }
    if (status == SYS_OK) {
        status = metadata_read(reliability, copy_slot, &readback);
    }
    if (status == SYS_OK &&
        memcmp(&readback, metadata, sizeof(readback)) != 0) {
        status = ERR_FLASH_VERIFY;
    }
    if (status == SYS_OK) {
        status = boot_meta_validate(&readback);
    }
    lock_status = f407_flash_lock(&reliability->flash);
    return status == SYS_OK ? lock_status : status;
}

static app_slot_t running_slot(void)
{
#if FIRMWARE_VECTOR_OFFSET == 0x00020000U
    return SLOT_A;
#elif FIRMWARE_VECTOR_OFFSET == 0x00080000U
    return SLOT_B;
#else
    return SLOT_NONE;
#endif
}

static void fill_reliability_config(app_reliability_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->watchdog_ops = &watchdog_ops;
    config->watchdog_context = &reliability_context;
    config->fault_recorder_ops = &fault_recorder_ops;
    config->fault_recorder_context = &reliability_context;
    config->fault_injection_enabled =
        F407_ENABLE_FAULT_INJECTION != 0 ? 1u : 0u;
    config->watchdog_timeout_ms = F407_WATCHDOG_TIMEOUT_MS;
    config->metadata_store.read = metadata_read;
    config->metadata_store.write = metadata_write;
    config->metadata_store.context = &reliability_context;
    config->running_slot = running_slot();
    config->power.auto_eco_after_ms = 30000u;
    config->power.minimum_tickless_ms = 5u;
    config->power.watchdog_margin_ms = 1500u;
    config->power.lock_leak_timeout_ms = 600000u;
    config->power.persistent_lock_mask =
        (1UL << PM_LOCK_CAN_MONITORING) | (1UL << PM_LOCK_ALARM_ACTIVE);
    config->deep_power_ops = &deep_power_ops;
    config->deep_power_context = &reliability_context;
    config->deep_power.stop_enabled = F407_STOP_PERIODIC_ENABLED;
    config->deep_power.standby_enabled = F407_STANDBY_SHIPPING_ENABLED;
    config->deep_power.minimum_stop_ms = F407_STOP_MINIMUM_MS;
    config->deep_power.maximum_stop_ms = F407_STOP_MAXIMUM_MS;
    config->deep_power.watchdog_margin_ms = 1500u;
    config->deep_power.quiesce_timeout_ms = 5000u;
    config->deep_power.required_quiesce_mask =
        DEEP_POWER_PARTICIPANT_ACQUISITION | DEEP_POWER_PARTICIPANT_MODBUS |
        DEEP_POWER_PARTICIPANT_CAN | DEEP_POWER_PARTICIPANT_NETWORK |
        DEEP_POWER_PARTICIPANT_STORAGE | DEEP_POWER_PARTICIPANT_UI;
    config->supervisor.task_count = GATEWAY_TASK_COUNT;
    config->supervisor.critical_task_mask =
        (1UL << GATEWAY_TASK_ACQUISITION) | (1UL << GATEWAY_TASK_DATA_HUB) |
        (1UL << GATEWAY_TASK_MODBUS) | (1UL << GATEWAY_TASK_CAN) |
        (1UL << GATEWAY_TASK_NETWORK) | (1UL << GATEWAY_TASK_OTA) |
        (1UL << GATEWAY_TASK_STORAGE) | (1UL << GATEWAY_TASK_UI);
    config->supervisor.task_timeout_ms[GATEWAY_TASK_ACQUISITION] = 500u;
    config->supervisor.task_timeout_ms[GATEWAY_TASK_DATA_HUB] = 2000u;
    config->supervisor.task_timeout_ms[GATEWAY_TASK_MODBUS] = 2500u;
    config->supervisor.task_timeout_ms[GATEWAY_TASK_CAN] = 500u;
    config->supervisor.task_timeout_ms[GATEWAY_TASK_NETWORK] = 2500u;
    config->supervisor.task_timeout_ms[GATEWAY_TASK_OTA] = 6000u;
    config->supervisor.task_timeout_ms[GATEWAY_TASK_STORAGE] = 4000u;
    config->supervisor.task_timeout_ms[GATEWAY_TASK_UI] = 500u;
    config->supervisor.startup_grace_ms = 3500u;
    config->supervisor.boot_confirm_stable_ms = 2000u;
}

status_t f407_reliability_configure(app_context_t *context)
{
    app_reliability_config_t config;
    f407_flash_port_t flash_port;
    status_t status;

    if (context == 0 || running_slot() == SLOT_NONE) {
        return ERR_INVALID_ARG;
    }
    memset(&reliability_context, 0, sizeof(reliability_context));
    memset(&flash_port, 0, sizeof(flash_port));
    flash_port.unlock = flash_unlock;
    flash_port.lock = flash_lock;
    flash_port.erase_sector = flash_erase_sector;
    flash_port.program_word = flash_program_word;
    flash_port.read = flash_read;
    flash_port.context = &reliability_context;
    status = f407_flash_init(&reliability_context.flash, &flash_port);
    if (status != SYS_OK) {
        return status;
    }

    fill_reliability_config(&config);

    status = app_context_configure_reliability(context, &config);
    if (status == SYS_OK) {
        platform_f407_bind_reliability(&context->power, &context->watchdog);
    }
    return status;
}
