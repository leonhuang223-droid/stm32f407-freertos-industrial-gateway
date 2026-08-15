#include "FreeRTOS.h"
#include "task.h"

#include "platform_f407.h"

static StaticTask_t idle_task_control_block;
static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];
static StaticTask_t timer_task_control_block;
static StackType_t timer_task_stack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetIdleTaskMemory(StaticTask_t **task_buffer,
                                   StackType_t **stack_buffer,
                                   configSTACK_DEPTH_TYPE *stack_size)
{
    *task_buffer = &idle_task_control_block;
    *stack_buffer = idle_task_stack;
    *stack_size = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **task_buffer,
                                    StackType_t **stack_buffer,
                                    configSTACK_DEPTH_TYPE *stack_size)
{
    *task_buffer = &timer_task_control_block;
    *stack_buffer = timer_task_stack;
    *stack_size = configTIMER_TASK_STACK_DEPTH;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    platform_f407_panic();
}

void vApplicationMallocFailedHook(void)
{
    platform_f407_panic();
}
