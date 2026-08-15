#ifndef F407_FAULT_CAPTURE_H
#define F407_FAULT_CAPTURE_H

#include "fault_recorder.h"

#include <stdint.h>

status_t f407_fault_record_load(fault_record_t *record);
status_t f407_fault_record_clear(void);
status_t f407_fault_prepare_watchdog_injection(void);
void f407_fault_inject_hardfault(void);
void f407_fault_capture_exception(const uint32_t *stack_frame,
                                  uint32_t exception_return,
                                  fault_origin_t origin);

void platform_runtime_stats_configure(void);
uint32_t platform_runtime_stats_counter(void);
void platform_fault_note_task(const volatile void *task_handle);

#endif
