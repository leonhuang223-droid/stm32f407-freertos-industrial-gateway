#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

extern uint32_t SystemCoreClock;
void platform_assert_panic(const char *file, int line);
void platform_f407_pre_sleep(uint32_t *expected_idle_ticks);
void platform_f407_post_sleep(uint32_t expected_idle_ticks);
void platform_runtime_stats_configure(void);
uint32_t platform_runtime_stats_counter(void);
void platform_fault_note_task(const volatile void *task_handle);

#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE 1
#define configCPU_CLOCK_HZ (SystemCoreClock)
#define configTICK_RATE_HZ 1000U
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#define configMAX_PRIORITIES 8U
#define configMINIMAL_STACK_SIZE 128U
#define configMAX_TASK_NAME_LEN 16U
#define configIDLE_SHOULD_YIELD 1
#define configUSE_TASK_NOTIFICATIONS 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 1
#define configUSE_COUNTING_SEMAPHORES 1
#define configQUEUE_REGISTRY_SIZE 16U
#define configUSE_QUEUE_SETS 1
#define configUSE_NEWLIB_REENTRANT 0
#define configENABLE_BACKWARD_COMPATIBILITY 0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0

#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0

#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK 1

#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY (configMAX_PRIORITIES - 1U)
#define configTIMER_QUEUE_LENGTH 8U
#define configTIMER_TASK_STACK_DEPTH 256U

#define configUSE_TRACE_FACILITY 1
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#define configGENERATE_RUN_TIME_STATS 1
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()                               \
    platform_runtime_stats_configure()
#define portGET_RUN_TIME_COUNTER_VALUE() platform_runtime_stats_counter()
#define traceTASK_SWITCHED_IN() platform_fault_note_task(pxCurrentTCB)

#define INCLUDE_vTaskDelay 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskPrioritySet 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_eTaskGetState 1

#define configPRIO_BITS 4U
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15U
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5U
#define configKERNEL_INTERRUPT_PRIORITY                                        \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8U - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY                                   \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8U - configPRIO_BITS))

#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define configPRE_SLEEP_PROCESSING(ticks) platform_f407_pre_sleep(&(ticks))
#define configPOST_SLEEP_PROCESSING(ticks) platform_f407_post_sleep((ticks))

#define configASSERT(condition)                                                \
    do {                                                                       \
        if ((condition) == 0) {                                                \
            platform_assert_panic(__FILE__, __LINE__);                         \
        }                                                                      \
    } while (0)

#endif
