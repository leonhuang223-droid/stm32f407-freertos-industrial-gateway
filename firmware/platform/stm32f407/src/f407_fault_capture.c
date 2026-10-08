#include "f407_fault_capture.h"

#include "main.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define F407_RTC_BACKUP_REGISTER_COUNT 20u
#define F407_SRAM_BASE 0x20000000u
#define F407_SRAM_END 0x20020000u
#define F407_CCM_BASE 0x10000000u
#define F407_CCM_END 0x10010000u

typedef char fault_record_fits_backup_registers
    [sizeof(fault_record_t) <= F407_RTC_BACKUP_REGISTER_COUNT * sizeof(uint32_t)
         ? 1
         : -1];
typedef char fault_record_is_word_aligned
    [sizeof(fault_record_t) % sizeof(uint32_t) == 0u ? 1 : -1];

static volatile uint32_t active_task_token;

static volatile uint32_t *backup_registers(void)
{
    return &RTC->BKP0R;
}

static void enable_backup_write(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR |= PWR_CR_DBP;
    (void)PWR->CR;
    __DSB();
}

static void read_backup_words(fault_record_t *record)
{
    uint32_t words[sizeof(fault_record_t) / sizeof(uint32_t)];
    volatile uint32_t *registers = backup_registers();
    size_t i;

    for (i = 0u; i < sizeof(words) / sizeof(words[0]); ++i) {
        words[i] = registers[i];
    }
    memcpy(record, words, sizeof(*record));
}

static status_t write_record(fault_record_t *record)
{
    uint32_t words[sizeof(fault_record_t) / sizeof(uint32_t)];
    volatile uint32_t *registers = backup_registers();
    size_t i;
    status_t status = fault_record_finalize(record);

    if (status != SYS_OK) {
        return status;
    }
    memcpy(words, record, sizeof(words));
    enable_backup_write();
    registers[0] = 0u;
    for (i = 1u; i < sizeof(words) / sizeof(words[0]); ++i) {
        registers[i] = words[i];
    }
    __DSB();
    registers[0] = words[0];
    __DSB();
    return SYS_OK;
}

static uint32_t next_sequence(void)
{
    fault_record_t previous;

    read_backup_words(&previous);
    return fault_record_validate(&previous) == SYS_OK ? previous.sequence + 1u
                                                      : 1u;
}

static uint8_t stack_frame_is_readable(const uint32_t *stack_frame)
{
    uintptr_t start = (uintptr_t)stack_frame;
    uintptr_t end = start + 8u * sizeof(uint32_t);

    return ((start >= F407_SRAM_BASE && end <= F407_SRAM_END) ||
            (start >= F407_CCM_BASE && end <= F407_CCM_END)) &&
                   end >= start
               ? 1u
               : 0u;
}

status_t f407_fault_record_load(fault_record_t *record)
{
    if (record == 0) {
        return ERR_INVALID_ARG;
    }
    read_backup_words(record);
    return record->magic == FAULT_RECORD_MAGIC ? SYS_OK : ERR_DEVICE_NOT_READY;
}

status_t f407_fault_record_clear(void)
{
    volatile uint32_t *registers = backup_registers();
    size_t i;

    enable_backup_write();
    for (i = 0u; i < sizeof(fault_record_t) / sizeof(uint32_t); ++i) {
        registers[i] = 0u;
    }
    __DSB();
    return SYS_OK;
}

status_t f407_fault_prepare_watchdog_injection(void)
{
    fault_record_t record;

    memset(&record, 0, sizeof(record));
    record.origin = FAULT_ORIGIN_WATCHDOG_INJECTION;
    record.sequence = next_sequence();
    record.reset_flags = RCC->CSR;
    record.task_token = active_task_token;
    return write_record(&record);
}

void f407_fault_capture_exception(const uint32_t *stack_frame,
                                  uint32_t exception_return,
                                  fault_origin_t origin)
{
    fault_record_t record;

    __disable_irq();
    memset(&record, 0, sizeof(record));
    record.origin = (uint8_t)origin;
    record.sequence = next_sequence();
    record.reset_flags = RCC->CSR;
    record.exception_return = exception_return;
    record.task_token = active_task_token;
    if (stack_frame_is_readable(stack_frame) != 0u) {
        record.stacked_r0 = stack_frame[0];
        record.stacked_r1 = stack_frame[1];
        record.stacked_r2 = stack_frame[2];
        record.stacked_r3 = stack_frame[3];
        record.stacked_r12 = stack_frame[4];
        record.stacked_lr = stack_frame[5];
        record.stacked_pc = stack_frame[6];
        record.stacked_xpsr = stack_frame[7];
    }
    record.cfsr = SCB->CFSR;
    record.hfsr = SCB->HFSR;
    record.mmfar = SCB->MMFAR;
    record.bfar = SCB->BFAR;
    (void)write_record(&record);
    __DSB();
    NVIC_SystemReset();
    for (;;) {
    }
}

void f407_fault_inject_hardfault(void)
{
    __asm volatile("udf #0");
    for (;;) {
    }
}

void platform_runtime_stats_configure(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t platform_runtime_stats_counter(void)
{
    return DWT->CYCCNT;
}

void platform_fault_note_task(const volatile void *task_handle)
{
    active_task_token = (uint32_t)(uintptr_t)task_handle;
}
