#include "f407_boot_platform.h"

#include "boot_jump.h"
#include "boot_reset.h"
#include "boot_staging_package.h"
#include "f407_flash.h"
#include "image_descriptor.h"
#include "image_install.h"
#include "main.h"
#include "platform_f407.h"
#include "spi.h"
#include "usart.h"
#include "w25q_boot.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BOOT_IO_TIMEOUT_MS 100u
#define BOOT_INSTALL_SCRATCH_SIZE 256u

typedef struct {
    uint32_t base_address;
} f407_address_reader_t;

typedef struct {
    f407_flash_t flash;
    w25q_boot_t w25q128;
    app_slot_t install_active_slot;
    app_slot_t descriptor_slot;
    uint32_t install_package_size;
    boot_reset_reason_t reset_reason;
    status_t w25_status;
    uint8_t reset_reason_valid;
    uint8_t scratch[BOOT_INSTALL_SCRATCH_SIZE];
} f407_boot_context_t;

static f407_boot_context_t boot_context;

static status_t map_hal_status(HAL_StatusTypeDef status,
                               status_t failure_status)
{
    if (status == HAL_OK) {
        return SYS_OK;
    }
    return status == HAL_TIMEOUT ? ERR_TIMEOUT : failure_status;
}

static status_t hal_flash_unlock(void *context)
{
    (void)context;
    return HAL_FLASH_Unlock() == HAL_OK ? SYS_OK : ERR_FLASH_WRITE;
}

static status_t hal_flash_lock(void *context)
{
    (void)context;
    return HAL_FLASH_Lock() == HAL_OK ? SYS_OK : ERR_FLASH_WRITE;
}

static status_t hal_flash_erase_sector(void *context, uint8_t sector_index)
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
hal_flash_program_word(void *context, uint32_t address, uint32_t value)
{
    (void)context;
    return HAL_FLASH_Program(
               FLASH_TYPEPROGRAM_WORD, address, (uint64_t)value) == HAL_OK
               ? SYS_OK
               : ERR_FLASH_WRITE;
}

static status_t
hal_flash_read(void *context, uint32_t address, uint8_t *buffer, size_t length)
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

static status_t w25_select(void *context, int active)
{
    (void)context;
    HAL_GPIO_WritePin(W25Q128_CS_GPIO_Port,
                      W25Q128_CS_Pin,
                      active != 0 ? GPIO_PIN_RESET : GPIO_PIN_SET);
    return SYS_OK;
}

static status_t w25_transmit(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    if (data == 0 || length == 0u || length > UINT16_MAX) {
        return ERR_INVALID_ARG;
    }
    return map_hal_status(HAL_SPI_Transmit(&hspi1,
                                           (uint8_t *)(uintptr_t)data,
                                           (uint16_t)length,
                                           BOOT_IO_TIMEOUT_MS),
                          ERR_FLASH_WRITE);
}

static status_t w25_receive(void *context, uint8_t *data, size_t length)
{
    (void)context;
    if (data == 0 || length == 0u || length > UINT16_MAX) {
        return ERR_INVALID_ARG;
    }
    return map_hal_status(
        HAL_SPI_Receive(&hspi1, data, (uint16_t)length, BOOT_IO_TIMEOUT_MS),
        ERR_FLASH_VERIFY);
}

static void w25_delay(void *context, uint32_t delay_ms)
{
    (void)context;
    HAL_Delay(delay_ms);
}

static status_t
staging_read(void *context, uint32_t address, uint8_t *buffer, size_t length)
{
    f407_boot_context_t *platform = context;

    if (platform == 0) {
        return ERR_INVALID_ARG;
    }
    if (platform->w25_status != SYS_OK) {
        return platform->w25_status;
    }
    return w25q_boot_read(&platform->w25q128, address, buffer, length);
}

static boot_reset_reason_t capture_reset_reason(void)
{
    uint32_t flags = 0u;

    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET) {
        flags |= BOOT_RCC_CSR_IWDGRSTF;
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != RESET) {
        flags |= BOOT_RCC_CSR_WWDGRSTF;
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != RESET) {
        flags |= BOOT_RCC_CSR_SFTRSTF;
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != RESET) {
        flags |= BOOT_RCC_CSR_PORRSTF;
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != RESET) {
        flags |= BOOT_RCC_CSR_PINRSTF;
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != RESET) {
        flags |= BOOT_RCC_CSR_LPWRRSTF;
    }
    return boot_reset_reason_from_rcc_csr(flags);
}

static status_t minimal_init(void *context)
{
    f407_boot_context_t *platform = context;
    f407_flash_port_t flash_port;
    w25q_boot_config_t w25_config;
    w25q_boot_port_t w25_port;
    status_t status;

    if (platform == 0) {
        return ERR_INVALID_ARG;
    }
    platform->reset_reason = capture_reset_reason();
    platform->reset_reason_valid = 1u;

    status = platform_f407_init();
    if (status != SYS_OK) {
        return status;
    }
    __HAL_RCC_CLEAR_RESET_FLAGS();

    memset(&flash_port, 0, sizeof(flash_port));
    flash_port.unlock = hal_flash_unlock;
    flash_port.lock = hal_flash_lock;
    flash_port.erase_sector = hal_flash_erase_sector;
    flash_port.program_word = hal_flash_program_word;
    flash_port.read = hal_flash_read;
    flash_port.context = platform;
    status = f407_flash_init(&platform->flash, &flash_port);
    if (status != SYS_OK) {
        return status;
    }

    memset(&w25_port, 0, sizeof(w25_port));
    w25_port.select = w25_select;
    w25_port.transmit = w25_transmit;
    w25_port.receive = w25_receive;
    w25_port.delay_ms = w25_delay;
    w25_port.context = platform;
    w25_config.expected_jedec_id = W25Q128_JEDEC_ID;
    w25_config.total_size = W25Q128_TOTAL_SIZE;
    platform->w25_status =
        w25q_boot_init(&platform->w25q128, &w25_config, &w25_port);

    /* A staging-device fault must not prevent booting a valid internal App. */
    return SYS_OK;
}

static status_t get_reset_reason(void *context, boot_reset_reason_t *out_reason)
{
    f407_boot_context_t *platform = context;

    if (platform == 0 || out_reason == 0 ||
        platform->reset_reason_valid == 0u) {
        return ERR_INVALID_ARG;
    }
    *out_reason = platform->reset_reason;
    return SYS_OK;
}

static status_t metadata_read(void *context,
                              app_slot_t copy_slot,
                              boot_metadata_t *out_metadata)
{
    f407_boot_context_t *platform = context;
    const partition_t *partition = partition_get_metadata(copy_slot);

    if (platform == 0 || partition == 0 || out_metadata == 0) {
        return ERR_INVALID_ARG;
    }
    return f407_flash_read(&platform->flash,
                           partition->start,
                           (uint8_t *)out_metadata,
                           sizeof(*out_metadata));
}

static status_t metadata_write(void *context,
                               app_slot_t copy_slot,
                               const boot_metadata_t *metadata)
{
    f407_boot_context_t *platform = context;
    boot_metadata_t readback;
    status_t status;
    status_t lock_status;

    if (platform == 0 || metadata == 0) {
        return ERR_INVALID_ARG;
    }
    status = boot_meta_validate(metadata);
    if (status != SYS_OK) {
        return status;
    }
    status = f407_flash_unlock(&platform->flash);
    if (status != SYS_OK) {
        return status;
    }
    status = f407_flash_erase_metadata(&platform->flash, copy_slot);
    if (status == SYS_OK) {
        status = f407_flash_program_metadata(&platform->flash,
                                             copy_slot,
                                             (const uint8_t *)metadata,
                                             sizeof(*metadata));
    }
    if (status == SYS_OK) {
        status = metadata_read(platform, copy_slot, &readback);
    }
    if (status == SYS_OK &&
        memcmp(&readback, metadata, sizeof(readback)) != 0) {
        status = ERR_FLASH_VERIFY;
    }
    if (status == SYS_OK) {
        status = boot_meta_validate(&readback);
    }
    lock_status = f407_flash_lock(&platform->flash);
    return status == SYS_OK ? lock_status : status;
}

static status_t
read_relative(void *context, uint32_t offset, uint8_t *buffer, size_t length)
{
    f407_address_reader_t *reader = context;

    if (reader == 0 || offset > UINT32_MAX - reader->base_address) {
        return ERR_INVALID_ARG;
    }
    return hal_flash_read(0, reader->base_address + offset, buffer, length);
}

static status_t verify_installed_slot(f407_boot_context_t *platform,
                                      app_slot_t slot,
                                      image_header_t *out_header)
{
    const partition_t *image = partition_get_slot_image(slot);
    const partition_t *descriptor = partition_get_slot_descriptor(slot);
    f407_address_reader_t image_reader;
    f407_address_reader_t descriptor_reader;

    if (platform == 0 || image == 0 || descriptor == 0) {
        return ERR_SLOT_MISMATCH;
    }
    image_reader.base_address = image->start;
    descriptor_reader.base_address = descriptor->start;
    return image_descriptor_verify_installed(
        slot,
        &(const image_descriptor_check_t){read_relative,
                                          &descriptor_reader,
                                          read_relative,
                                          &image_reader,
                                          platform->scratch,
                                          sizeof(platform->scratch),
                                          out_header});
}

static status_t validate_slot(void *context, app_slot_t slot)
{
    return verify_installed_slot(context, slot, 0);
}

static status_t descriptor_store_read(void *context,
                                      uint32_t address,
                                      uint8_t *buffer,
                                      size_t length)
{
    f407_boot_context_t *platform = context;

    return platform != 0
               ? f407_flash_read(&platform->flash, address, buffer, length)
               : ERR_INVALID_ARG;
}

static status_t descriptor_erase(void *context, uint32_t address, size_t length)
{
    f407_boot_context_t *platform = context;
    const partition_t *descriptor;

    if (platform == 0) {
        return ERR_INVALID_ARG;
    }
    descriptor = partition_get_slot_descriptor(platform->descriptor_slot);
    if (descriptor == 0 || address != descriptor->start ||
        length != descriptor->size) {
        return ERR_FLASH_ERASE;
    }
    return f407_flash_erase_descriptor(&platform->flash,
                                       platform->descriptor_slot);
}

static status_t descriptor_write(void *context,
                                 uint32_t address,
                                 const uint8_t *data,
                                 size_t length)
{
    f407_boot_context_t *platform = context;
    const partition_t *descriptor;

    if (platform == 0) {
        return ERR_INVALID_ARG;
    }
    descriptor = partition_get_slot_descriptor(platform->descriptor_slot);
    if (descriptor == 0 || address != descriptor->start || data == 0 ||
        length == 0u || length > descriptor->size) {
        return ERR_FLASH_WRITE;
    }
    return f407_flash_program_descriptor(
        &platform->flash, platform->descriptor_slot, data, length);
}

static status_t confirm_slot(void *context, app_slot_t slot)
{
    f407_boot_context_t *platform = context;
    image_descriptor_store_t store;
    image_header_t header;
    status_t status;
    status_t lock_status;

    status = verify_installed_slot(platform, slot, &header);
    if (status != SYS_OK) {
        return status;
    }
    if (image_header_get_state(&header) == IMAGE_STATE_CONFIRMED) {
        return SYS_OK;
    }
    if (image_header_get_state(&header) != IMAGE_STATE_CANDIDATE) {
        return ERR_IMAGE_INVALID;
    }

    platform->descriptor_slot = slot;
    memset(&store, 0, sizeof(store));
    store.read = descriptor_store_read;
    store.erase = descriptor_erase;
    store.write = descriptor_write;
    store.context = platform;

    status = f407_flash_unlock(&platform->flash);
    if (status != SYS_OK) {
        return status;
    }
    status = image_descriptor_confirm_candidate(
        &store, slot, platform->scratch, sizeof(platform->scratch), 0);
    lock_status = f407_flash_lock(&platform->flash);
    return status == SYS_OK ? lock_status : status;
}

static status_t validate_staging(void *context, const boot_metadata_t *metadata)
{
    f407_boot_context_t *platform = context;

    return boot_staging_package_validate(
        staging_read,
        &(const boot_package_validation_t){platform,
                                           metadata,
                                           platform->scratch,
                                           sizeof(platform->scratch),
                                           0,
                                           0});
}

static status_t install_package_read(void *context,
                                     uint32_t offset,
                                     uint8_t *buffer,
                                     size_t length)
{
    f407_boot_context_t *platform = context;

    if (platform == 0 || buffer == 0 || length == 0u ||
        offset > platform->install_package_size ||
        length > (size_t)(platform->install_package_size - offset)) {
        return ERR_INVALID_ARG;
    }
    return w25q_boot_read(
        &platform->w25q128, BOOT_STAGING_START + offset, buffer, length);
}

static status_t
install_flash_erase(void *context, uint32_t address, size_t length)
{
    f407_boot_context_t *platform = context;

    return f407_flash_erase_inactive(
        &platform->flash, address, length, platform->install_active_slot);
}

static status_t install_flash_write(void *context,
                                    uint32_t address,
                                    const uint8_t *data,
                                    size_t length)
{
    f407_boot_context_t *platform = context;

    return f407_flash_program_inactive(
        &platform->flash, address, data, length, platform->install_active_slot);
}

static status_t install_flash_read(void *context,
                                   uint32_t address,
                                   uint8_t *buffer,
                                   size_t length)
{
    f407_boot_context_t *platform = context;

    return f407_flash_read(&platform->flash, address, buffer, length);
}

static status_t program_inactive_slot(void *context,
                                      const boot_metadata_t *metadata)
{
    f407_boot_context_t *platform = context;
    boot_staging_package_t package;
    image_install_port_t install_port;
    status_t status;
    status_t lock_status;

    status = boot_staging_package_validate(
        staging_read,
        &(const boot_package_validation_t){platform,
                                           metadata,
                                           platform->scratch,
                                           sizeof(platform->scratch),
                                           &package,
                                           0});
    if (status != SYS_OK) {
        return status;
    }

    platform->install_active_slot = metadata->active_slot;
    platform->install_package_size = package.package_size;
    memset(&install_port, 0, sizeof(install_port));
    install_port.package_read = install_package_read;
    install_port.flash_erase = install_flash_erase;
    install_port.flash_write = install_flash_write;
    install_port.flash_read = install_flash_read;
    install_port.context = platform;

    status = f407_flash_unlock(&platform->flash);
    if (status != SYS_OK) {
        return status;
    }
    status = image_install_package(
        &install_port,
        &(const image_install_request_t){metadata->active_slot,
                                         metadata->pending_slot,
                                         package.package_size,
                                         platform->scratch,
                                         sizeof(platform->scratch),
                                         0});
    lock_status = f407_flash_lock(&platform->flash);
    return status == SYS_OK ? lock_status : status;
}

static status_t scan_recovery(void *context,
                              boot_meta_recovery_scan_t *out_scan)
{
    f407_boot_context_t *platform = context;
    image_descriptor_store_t store;
    image_descriptor_scan_status_t scan_status;

    if (platform == 0 || out_scan == 0) {
        return ERR_INVALID_ARG;
    }
    memset(&store, 0, sizeof(store));
    store.read = descriptor_store_read;
    store.context = platform;
    return image_descriptor_scan_recovery(&store,
                                          platform->scratch,
                                          sizeof(platform->scratch),
                                          out_scan,
                                          &scan_status);
}

static status_t
jump_read(void *context, uint32_t address, uint8_t *buffer, size_t length)
{
    f407_boot_context_t *platform = context;

    return f407_flash_read(&platform->flash, address, buffer, length);
}

static void jump_disable_interrupts(void *context)
{
    (void)context;
    __disable_irq();
    __DSB();
    __ISB();
}

static void jump_clear_nvic(void *context)
{
    size_t i;

    (void)context;
    for (i = 0u; i < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]); ++i) {
        NVIC->ICER[i] = UINT32_MAX;
        NVIC->ICPR[i] = UINT32_MAX;
    }
    __DSB();
    __ISB();
}

static void jump_stop_tick(void *context)
{
    (void)context;
    HAL_SuspendTick();
    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL = 0u;
}

static void jump_deinit_peripherals(void *context)
{
    (void)context;
    HAL_GPIO_WritePin(W25Q128_CS_GPIO_Port, W25Q128_CS_Pin, GPIO_PIN_SET);
    (void)HAL_SPI_DeInit(&hspi1);
    (void)HAL_UART_DeInit(&huart1);
    (void)HAL_DeInit();
}

static void jump_set_vtor(void *context, uint32_t address)
{
    (void)context;
    SCB->VTOR = address;
    __DSB();
    __ISB();
}

__attribute__((noreturn)) static void
jump_set_msp_and_branch(void *context, uint32_t msp, uint32_t reset_handler)
{
    (void)context;
    __set_BASEPRI(0u);
    __set_FAULTMASK(0u);
    __set_CONTROL(0u);
    __asm volatile("msr msp, %0\n"
                   "dsb\n"
                   "isb\n"
                   "bx %1\n"
                   :
                   : "r"(msp), "r"(reset_handler)
                   : "memory");
    __builtin_unreachable();
}

static status_t jump_to_slot(void *context, app_slot_t slot)
{
    boot_jump_port_t port;

    memset(&port, 0, sizeof(port));
    port.read = jump_read;
    port.disable_interrupts = jump_disable_interrupts;
    port.clear_nvic = jump_clear_nvic;
    port.stop_tick = jump_stop_tick;
    port.deinit_peripherals = jump_deinit_peripherals;
    port.set_vtor = jump_set_vtor;
    port.set_msp_and_branch = jump_set_msp_and_branch;
    port.context = context;
    return boot_jump_to_slot(&port, slot);
}

static void diagnostic_write(const char *message, size_t length)
{
    if (message != 0 && length != 0u && length <= UINT16_MAX) {
        (void)HAL_UART_Transmit(&huart1,
                                (uint8_t *)(uintptr_t)message,
                                (uint16_t)length,
                                BOOT_IO_TIMEOUT_MS);
    }
}

static void diagnostic_status(status_t reason)
{
    static const char prefix[] = "F407 BOOT maintenance: ";
    static const char suffix[] = "\r\n";
    const char *text = error_to_string(reason);
    size_t length = 0u;

    while (text[length] != '\0' && length < 31u) {
        length++;
    }
    diagnostic_write(prefix, sizeof(prefix) - 1u);
    diagnostic_write(text, length);
    diagnostic_write(suffix, sizeof(suffix) - 1u);
}

__attribute__((noreturn)) static void maintenance_loop(status_t reason)
{
    uint32_t last_toggle = HAL_GetTick();
    GPIO_PinState state = GPIO_PIN_RESET;

    diagnostic_status(reason);
    HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, state);
    for (;;) {
        uint32_t now = HAL_GetTick();
        if ((uint32_t)(now - last_toggle) >= 500u) {
            last_toggle = now;
            state = state == GPIO_PIN_RESET ? GPIO_PIN_SET : GPIO_PIN_RESET;
            HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, state);
        }
        __WFI();
    }
}

static status_t enter_maintenance(void *context, status_t reason)
{
    (void)context;
    maintenance_loop(reason);
}

int f407_boot_platform_key_pressed(void)
{
    return 0;
}

void f407_boot_platform_fatal(status_t reason)
{
    maintenance_loop(reason);
}

status_t f407_boot_platform_make_port(bootloader_port_t *out_port)
{
    if (out_port == 0) {
        return ERR_INVALID_ARG;
    }
    memset(&boot_context, 0, sizeof(boot_context));
    boot_context.install_active_slot = SLOT_NONE;
    boot_context.descriptor_slot = SLOT_NONE;

    memset(out_port, 0, sizeof(*out_port));
    out_port->minimal_init = minimal_init;
    out_port->get_reset_reason = get_reset_reason;
    out_port->validate_slot = validate_slot;
    out_port->confirm_slot = confirm_slot;
    out_port->validate_staging = validate_staging;
    out_port->program_inactive_slot = program_inactive_slot;
    out_port->jump_to_slot = jump_to_slot;
    out_port->enter_maintenance = enter_maintenance;
    out_port->scan_recovery = scan_recovery;
    out_port->metadata_store.read = metadata_read;
    out_port->metadata_store.write = metadata_write;
    out_port->metadata_store.context = &boot_context;
    out_port->context = &boot_context;
    return SYS_OK;
}

int f407_boot_main(void)
{
    bootloader_port_t port;
    bootloader_decision_t decision;
    status_t status;

    memset(&port, 0, sizeof(port));
    status = f407_boot_platform_make_port(&port);
    if (status != SYS_OK) {
        f407_boot_platform_fatal(status);
    }
    status = bootloader_init(&port);
    if (status != SYS_OK) {
        f407_boot_platform_fatal(status);
    }
    status = bootloader_execute(f407_boot_platform_key_pressed(), &decision);
    f407_boot_platform_fatal(status);
    return 1;
}
