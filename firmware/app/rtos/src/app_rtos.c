#include "app_rtos.h"

#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "platform_constants.h"
#include "version.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define MEASUREMENT_QUEUE_LENGTH 16U
#define CAN_TX_QUEUE_LENGTH 8U
#define UI_SNAPSHOT_QUEUE_LENGTH 1U
#define NETWORK_TELEMETRY_QUEUE_LENGTH 1U
#define NETWORK_ALARM_QUEUE_LENGTH 8U
#define NETWORK_CONTROL_QUEUE_LENGTH 4U
#define NETWORK_CONTROL_RESULT_QUEUE_LENGTH 4U
#define OTA_COMMAND_QUEUE_LENGTH 4U
#define STORAGE_LOG_QUEUE_LENGTH 32U
#define STORAGE_ALARM_QUEUE_LENGTH 8U
#define STORAGE_CONFIG_QUEUE_LENGTH 4U
#define UI_COMMAND_QUEUE_LENGTH 8U
#define OTA_NETWORK_REQUEST_QUEUE_LENGTH 1U
#define OTA_STORAGE_REQUEST_QUEUE_LENGTH 1U
#define POWER_COMMAND_QUEUE_LENGTH 4U
#define STORAGE_QUEUE_SET_LENGTH \
    (STORAGE_LOG_QUEUE_LENGTH + STORAGE_ALARM_QUEUE_LENGTH + \
     STORAGE_CONFIG_QUEUE_LENGTH + OTA_STORAGE_REQUEST_QUEUE_LENGTH)
#define UI_ACTIVE_LOCK_HOLD_MS 3000u
#define APP_RTOS_SYSTEM_TASK_CAPACITY 16u
#define OTA_IO_NOTIFY_DONE (1UL << 0)
#define OTA_IO_WAIT_SLICE_MS 100u
#define STORAGE_ECO_IDLE_MS 5000u

#define SYSTEM_EVENT_STORAGE_READY (1UL << 0)
#define SYSTEM_EVENT_ACQUISITION_READY (1UL << 1)
#define SYSTEM_EVENT_FIELDBUS_READY (1UL << 2)
#define SYSTEM_EVENT_CONTROL_READY (1UL << 3)
#define SYSTEM_EVENT_FAULT_ACTIVE (1UL << 4)
#define SYSTEM_EVENT_ALARM_ACTIVE (1UL << 5)
#define SYSTEM_EVENT_NETWORK_UP (1UL << 6)
#define SYSTEM_EVENT_MQTT_READY (1UL << 7)
#define SYSTEM_EVENT_OTA_ACTIVE (1UL << 8)
#define SYSTEM_EVENT_UI_READY (1UL << 9)
#define SYSTEM_EVENT_CLI_READY (1UL << 10)
#define SYSTEM_EVENT_POWER_QUIESCE_REQUEST (1UL << 11)
#define SYSTEM_EVENT_POWER_ACK_ACQUISITION (1UL << 12)
#define SYSTEM_EVENT_POWER_ACK_MODBUS (1UL << 13)
#define SYSTEM_EVENT_POWER_ACK_CAN (1UL << 14)
#define SYSTEM_EVENT_POWER_ACK_NETWORK (1UL << 15)
#define SYSTEM_EVENT_POWER_ACK_STORAGE (1UL << 16)
#define SYSTEM_EVENT_POWER_ACK_UI (1UL << 17)
#define SYSTEM_EVENT_POWER_ACK_MASK \
    (SYSTEM_EVENT_POWER_ACK_ACQUISITION | SYSTEM_EVENT_POWER_ACK_MODBUS | \
     SYSTEM_EVENT_POWER_ACK_CAN | SYSTEM_EVENT_POWER_ACK_NETWORK | \
     SYSTEM_EVENT_POWER_ACK_STORAGE | SYSTEM_EVENT_POWER_ACK_UI)

typedef enum {
    OTA_NETWORK_FETCH_MANIFEST = 0,
    OTA_NETWORK_OPEN_PACKAGE,
    OTA_NETWORK_READ_PACKAGE,
    OTA_NETWORK_CLOSE_PACKAGE
} ota_network_operation_t;

typedef struct {
    ota_network_operation_t operation;
    const char *url;
    uint8_t *buffer;
    size_t capacity;
    size_t length;
    uint32_t content_length;
    status_t status;
    TaskHandle_t waiter;
} ota_network_request_t;

typedef enum {
    OTA_STORAGE_BEGIN = 0,
    OTA_STORAGE_ERASE_NEXT,
    OTA_STORAGE_WRITE,
    OTA_STORAGE_COMMIT_METADATA
} ota_storage_operation_t;

typedef struct {
    ota_storage_operation_t operation;
    uint32_t offset;
    const uint8_t *data;
    size_t length;
    uint8_t complete;
    status_t status;
    TaskHandle_t waiter;
} ota_storage_request_t;

typedef struct {
    app_context_t *context;
    uint8_t cancel_seen;
} ota_rtos_port_t;

typedef enum {
    POWER_COMMAND_REQUEST_STOP = 0,
    POWER_COMMAND_REQUEST_STANDBY,
    POWER_COMMAND_CANCEL
} power_command_operation_t;

typedef struct {
    power_command_operation_t operation;
    uint32_t duration_ms;
} power_command_t;

typedef struct {
    QueueHandle_t measurement;
    QueueHandle_t can_tx;
    QueueHandle_t ui_snapshot;
    QueueHandle_t network_telemetry;
    QueueHandle_t network_alarm;
    QueueHandle_t network_control;
    QueueHandle_t network_control_result;
    QueueHandle_t ota_command;
    QueueHandle_t storage_log;
    QueueHandle_t storage_alarm;
    QueueHandle_t storage_config;
    QueueHandle_t ota_network_request;
    QueueHandle_t ota_storage_request;
    QueueHandle_t ui_command;
    QueueHandle_t power_command;
    QueueSetHandle_t storage_set;
    EventGroupHandle_t system_events;
    SemaphoreHandle_t snapshot_mutex;
    SemaphoreHandle_t config_mutex;
} app_channel_handles_t;

typedef struct {
    uint8_t measurement[MEASUREMENT_QUEUE_LENGTH * sizeof(gateway_measurement_t)];
    uint8_t can_tx[CAN_TX_QUEUE_LENGTH * sizeof(gateway_can_tx_message_t)];
    uint8_t ui_snapshot[UI_SNAPSHOT_QUEUE_LENGTH * sizeof(gateway_system_snapshot_t)];
    uint8_t network_telemetry[NETWORK_TELEMETRY_QUEUE_LENGTH *
                              sizeof(gateway_network_event_t)];
    uint8_t network_alarm[NETWORK_ALARM_QUEUE_LENGTH *
                          sizeof(gateway_network_event_t)];
    uint8_t network_control[NETWORK_CONTROL_QUEUE_LENGTH *
                            sizeof(gateway_network_control_request_t)];
    uint8_t network_control_result[NETWORK_CONTROL_RESULT_QUEUE_LENGTH *
        sizeof(gateway_network_control_result_t)];
    uint8_t ota_command[OTA_COMMAND_QUEUE_LENGTH * sizeof(gateway_ota_command_t)];
    uint8_t storage_log[STORAGE_LOG_QUEUE_LENGTH *
                        sizeof(gateway_storage_log_request_t)];
    uint8_t storage_alarm[STORAGE_ALARM_QUEUE_LENGTH *
                          sizeof(gateway_storage_alarm_request_t)];
    uint8_t storage_config[STORAGE_CONFIG_QUEUE_LENGTH *
                           sizeof(gateway_storage_config_request_t)];
    uint8_t ota_network_request[OTA_NETWORK_REQUEST_QUEUE_LENGTH *
                                sizeof(ota_network_request_t *)];
    uint8_t ota_storage_request[OTA_STORAGE_REQUEST_QUEUE_LENGTH *
                                sizeof(ota_storage_request_t *)];
    uint8_t ui_command[UI_COMMAND_QUEUE_LENGTH * sizeof(ui_page_id_t)];
    uint8_t power_command[POWER_COMMAND_QUEUE_LENGTH *
                          sizeof(power_command_t)];
    uint8_t storage_set[STORAGE_QUEUE_SET_LENGTH * sizeof(QueueSetMemberHandle_t)];
} app_channel_storage_t;

typedef struct {
    StackType_t supervisor[512];
    StackType_t acquisition[768];
    StackType_t data_hub[768];
    StackType_t modbus[768];
    StackType_t can_task[512];
    StackType_t network[1024];
    StackType_t ota[1536];
    StackType_t storage[768];
    StackType_t ui[1536];
    StackType_t cli[768];
} app_task_stacks_t;

typedef void (*app_task_entry_t)(void *argument);

typedef struct {
    gateway_task_id_t id;
    const char *name;
    app_task_entry_t entry;
    UBaseType_t priority;
    uint32_t stack_words;
} app_task_spec_t;

static app_context_t *app_context;
static app_channel_handles_t channels;
static app_channel_storage_t channel_storage;
static StaticQueue_t queue_control_blocks[15];
static StaticQueue_t queue_set_control_block;
static StaticEventGroup_t system_event_control_block;
static StaticSemaphore_t snapshot_mutex_control_block;
static StaticSemaphore_t config_mutex_control_block;
static StaticTask_t task_control_blocks[GATEWAY_TASK_COUNT];
static TaskHandle_t task_handles[GATEWAY_TASK_COUNT];
static app_task_stacks_t task_stacks;
static uint32_t previous_runtime[GATEWAY_TASK_COUNT];
static uint32_t previous_idle_runtime;
static uint32_t previous_total_runtime;
static uint8_t runtime_baseline_valid;
static ota_rtos_port_t ota_rtos_port;

static void supervisor_task(void *argument);
static void acquisition_task(void *argument);
static void data_hub_task(void *argument);
static void modbus_task(void *argument);
static void can_task(void *argument);
static void network_task(void *argument);
static void ota_task(void *argument);
static void storage_task(void *argument);
static void ui_task(void *argument);
static void cli_task(void *argument);
static status_t handle_ui_action(void *opaque, const ui_action_t *action);
static status_t handle_cli_command(void *opaque,
                                   const cli_command_t *command,
                                   char *response, size_t capacity);
static status_t power_lock_acquire(app_context_t *context,
                                   power_lock_id_t lock);
static status_t power_lock_release(app_context_t *context,
                                   power_lock_id_t lock);
static void app_critical_enter(app_context_t *context);
static void app_critical_exit(app_context_t *context);
static void collect_rtos_diagnostics(app_context_t *context);
static const char *power_mode_name(power_mode_t mode);
static const char *power_policy_name(power_policy_t policy);
static const char *task_name_from_token(uint32_t task_token);
static uint32_t deep_power_quiesced_mask(EventBits_t bits);

static uint32_t deep_power_quiesced_mask(EventBits_t bits)
{
    uint32_t mask = 0u;

    if ((bits & SYSTEM_EVENT_POWER_ACK_ACQUISITION) != 0u) {
        mask |= DEEP_POWER_PARTICIPANT_ACQUISITION;
    }
    if ((bits & SYSTEM_EVENT_POWER_ACK_MODBUS) != 0u) {
        mask |= DEEP_POWER_PARTICIPANT_MODBUS;
    }
    if ((bits & SYSTEM_EVENT_POWER_ACK_CAN) != 0u) {
        mask |= DEEP_POWER_PARTICIPANT_CAN;
    }
    if ((bits & SYSTEM_EVENT_POWER_ACK_NETWORK) != 0u) {
        mask |= DEEP_POWER_PARTICIPANT_NETWORK;
    }
    if ((bits & SYSTEM_EVENT_POWER_ACK_STORAGE) != 0u) {
        mask |= DEEP_POWER_PARTICIPANT_STORAGE;
    }
    if ((bits & SYSTEM_EVENT_POWER_ACK_UI) != 0u) {
        mask |= DEEP_POWER_PARTICIPANT_UI;
    }
    return mask;
}

static const app_task_spec_t task_specs[GATEWAY_TASK_COUNT] = {
    { GATEWAY_TASK_SUPERVISOR, "Supervisor", supervisor_task, 6U, 512U },
    { GATEWAY_TASK_ACQUISITION, "Acquisition", acquisition_task, 5U, 768U },
    { GATEWAY_TASK_DATA_HUB, "DataHub", data_hub_task, 5U, 768U },
    { GATEWAY_TASK_MODBUS, "Modbus", modbus_task, 4U, 768U },
    { GATEWAY_TASK_CAN, "CAN", can_task, 4U, 512U },
    { GATEWAY_TASK_NETWORK, "Network", network_task, 4U, 1024U },
    { GATEWAY_TASK_OTA, "OTA", ota_task, 3U, 1536U },
    { GATEWAY_TASK_STORAGE, "Storage", storage_task, 2U, 768U },
    { GATEWAY_TASK_UI, "UI", ui_task, 2U, 1536U },
    { GATEWAY_TASK_CLI, "CLI", cli_task, 1U, 768U }
};

static StackType_t *task_stack(gateway_task_id_t id)
{
    switch (id) {
    case GATEWAY_TASK_SUPERVISOR: return task_stacks.supervisor;
    case GATEWAY_TASK_ACQUISITION: return task_stacks.acquisition;
    case GATEWAY_TASK_DATA_HUB: return task_stacks.data_hub;
    case GATEWAY_TASK_MODBUS: return task_stacks.modbus;
    case GATEWAY_TASK_CAN: return task_stacks.can_task;
    case GATEWAY_TASK_NETWORK: return task_stacks.network;
    case GATEWAY_TASK_OTA: return task_stacks.ota;
    case GATEWAY_TASK_STORAGE: return task_stacks.storage;
    case GATEWAY_TASK_UI: return task_stacks.ui;
    case GATEWAY_TASK_CLI: return task_stacks.cli;
    default: return 0;
    }
}

static status_t create_channels(void)
{
    channels.measurement = xQueueCreateStatic(
        MEASUREMENT_QUEUE_LENGTH, sizeof(gateway_measurement_t),
        channel_storage.measurement, &queue_control_blocks[0]);
    channels.can_tx = xQueueCreateStatic(
        CAN_TX_QUEUE_LENGTH, sizeof(gateway_can_tx_message_t),
        channel_storage.can_tx, &queue_control_blocks[1]);
    channels.ui_snapshot = xQueueCreateStatic(
        UI_SNAPSHOT_QUEUE_LENGTH, sizeof(gateway_system_snapshot_t),
        channel_storage.ui_snapshot, &queue_control_blocks[2]);
    channels.network_telemetry = xQueueCreateStatic(
        NETWORK_TELEMETRY_QUEUE_LENGTH, sizeof(gateway_network_event_t),
        channel_storage.network_telemetry, &queue_control_blocks[3]);
    channels.network_alarm = xQueueCreateStatic(
        NETWORK_ALARM_QUEUE_LENGTH, sizeof(gateway_network_event_t),
        channel_storage.network_alarm, &queue_control_blocks[4]);
    channels.network_control = xQueueCreateStatic(
        NETWORK_CONTROL_QUEUE_LENGTH,
        sizeof(gateway_network_control_request_t),
        channel_storage.network_control, &queue_control_blocks[5]);
    channels.network_control_result = xQueueCreateStatic(
        NETWORK_CONTROL_RESULT_QUEUE_LENGTH,
        sizeof(gateway_network_control_result_t),
        channel_storage.network_control_result, &queue_control_blocks[6]);
    channels.ota_command = xQueueCreateStatic(
        OTA_COMMAND_QUEUE_LENGTH, sizeof(gateway_ota_command_t),
        channel_storage.ota_command, &queue_control_blocks[7]);
    channels.storage_log = xQueueCreateStatic(
        STORAGE_LOG_QUEUE_LENGTH, sizeof(gateway_storage_log_request_t),
        channel_storage.storage_log, &queue_control_blocks[8]);
    channels.storage_alarm = xQueueCreateStatic(
        STORAGE_ALARM_QUEUE_LENGTH, sizeof(gateway_storage_alarm_request_t),
        channel_storage.storage_alarm, &queue_control_blocks[9]);
    channels.storage_config = xQueueCreateStatic(
        STORAGE_CONFIG_QUEUE_LENGTH, sizeof(gateway_storage_config_request_t),
        channel_storage.storage_config, &queue_control_blocks[10]);
    channels.ui_command = xQueueCreateStatic(
        UI_COMMAND_QUEUE_LENGTH, sizeof(ui_page_id_t),
        channel_storage.ui_command, &queue_control_blocks[11]);
    channels.ota_network_request = xQueueCreateStatic(
        OTA_NETWORK_REQUEST_QUEUE_LENGTH, sizeof(ota_network_request_t *),
        channel_storage.ota_network_request, &queue_control_blocks[12]);
    channels.ota_storage_request = xQueueCreateStatic(
        OTA_STORAGE_REQUEST_QUEUE_LENGTH, sizeof(ota_storage_request_t *),
        channel_storage.ota_storage_request, &queue_control_blocks[13]);
    channels.power_command = xQueueCreateStatic(
        POWER_COMMAND_QUEUE_LENGTH, sizeof(power_command_t),
        channel_storage.power_command, &queue_control_blocks[14]);
    channels.storage_set = xQueueCreateSetStatic(
        STORAGE_QUEUE_SET_LENGTH, channel_storage.storage_set,
        &queue_set_control_block);
    channels.system_events = xEventGroupCreateStatic(
        &system_event_control_block);
    channels.snapshot_mutex = xSemaphoreCreateMutexStatic(
        &snapshot_mutex_control_block);
    channels.config_mutex = xSemaphoreCreateMutexStatic(
        &config_mutex_control_block);

    if (channels.measurement == 0 || channels.can_tx == 0 ||
        channels.ui_snapshot == 0 ||
        channels.network_telemetry == 0 || channels.network_alarm == 0 ||
        channels.network_control == 0 ||
        channels.network_control_result == 0 ||
        channels.ota_command == 0 ||
        channels.storage_log == 0 || channels.storage_alarm == 0 ||
        channels.storage_config == 0 || channels.ui_command == 0 ||
        channels.ota_network_request == 0 ||
        channels.ota_storage_request == 0 ||
        channels.power_command == 0 ||
        channels.storage_set == 0 || channels.system_events == 0 ||
        channels.snapshot_mutex == 0 || channels.config_mutex == 0) {
        return ERR_NO_MEMORY;
    }
    if (xQueueAddToSet(channels.storage_log, channels.storage_set) != pdPASS ||
        xQueueAddToSet(channels.storage_alarm, channels.storage_set) != pdPASS ||
        xQueueAddToSet(channels.storage_config, channels.storage_set) != pdPASS ||
        xQueueAddToSet(channels.ota_storage_request,
                       channels.storage_set) != pdPASS) {
        return ERR_INVALID_ARG;
    }

    vQueueAddToRegistry(channels.measurement, "q_measurement");
    vQueueAddToRegistry(channels.can_tx, "q_can_tx");
    vQueueAddToRegistry(channels.ui_snapshot, "q_ui_snapshot");
    vQueueAddToRegistry(channels.network_telemetry, "q_network_telemetry");
    vQueueAddToRegistry(channels.network_alarm, "q_network_alarm");
    vQueueAddToRegistry(channels.network_control, "q_network_control");
    vQueueAddToRegistry(channels.network_control_result,
                        "q_network_control_result");
    vQueueAddToRegistry(channels.ota_command, "q_ota_command");
    vQueueAddToRegistry(channels.storage_log, "q_storage_log");
    vQueueAddToRegistry(channels.storage_alarm, "q_storage_alarm");
    vQueueAddToRegistry(channels.storage_config, "q_storage_config");
    vQueueAddToRegistry(channels.ui_command, "q_ui_command");
    vQueueAddToRegistry(channels.ota_network_request, "q_ota_network");
    vQueueAddToRegistry(channels.ota_storage_request, "q_ota_storage");
    vQueueAddToRegistry(channels.power_command, "q_power_command");
    return SYS_OK;
}

static status_t create_tasks(void)
{
    size_t i;

    for (i = 0u; i < GATEWAY_TASK_COUNT; ++i) {
        const app_task_spec_t *spec = &task_specs[i];
        TaskHandle_t task = xTaskCreateStatic(
            spec->entry, spec->name, spec->stack_words, app_context,
            spec->priority, task_stack(spec->id),
            &task_control_blocks[(unsigned int)spec->id]);
        if (task == 0) {
            return ERR_NO_MEMORY;
        }
        task_handles[(unsigned int)spec->id] = task;
    }
    return SYS_OK;
}

static void app_critical_enter(app_context_t *context)
{
    taskENTER_CRITICAL();
    if (context != 0) {
        (void)critical_timing_monitor_enter(
            &context->critical_timing,
            portGET_RUN_TIME_COUNTER_VALUE());
    }
}

static void app_critical_exit(app_context_t *context)
{
    if (context != 0) {
        (void)critical_timing_monitor_exit(
            &context->critical_timing,
            portGET_RUN_TIME_COUNTER_VALUE());
    }
    taskEXIT_CRITICAL();
}

static status_t power_lock_acquire(app_context_t *context,
                                   power_lock_id_t lock)
{
    status_t status;
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    app_critical_enter(context);
    status = power_manager_acquire(&context->power, lock, now_ms);
    app_critical_exit(context);
    return status;
}

static status_t power_lock_release(app_context_t *context,
                                   power_lock_id_t lock)
{
    status_t status;
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    app_critical_enter(context);
    status = power_manager_release(&context->power, lock, now_ms);
    app_critical_exit(context);
    return status;
}

static void collect_rtos_diagnostics(app_context_t *context)
{
    QueueHandle_t queues[15] = {
        channels.measurement,
        channels.can_tx,
        channels.ui_snapshot,
        channels.network_telemetry,
        channels.network_alarm,
        channels.network_control,
        channels.network_control_result,
        channels.ota_command,
        channels.storage_log,
        channels.storage_alarm,
        channels.storage_config,
        channels.ui_command,
        channels.ota_network_request,
        channels.ota_storage_request,
        channels.power_command
    };
    TaskStatus_t task_status[APP_RTOS_SYSTEM_TASK_CAPACITY];
    uint32_t current_runtime[GATEWAY_TASK_COUNT] = { 0u };
    uint32_t current_idle_runtime = 0u;
    uint32_t total_runtime = 0u;
    UBaseType_t status_count;
    unsigned int i;

    for (i = 0u; i < GATEWAY_TASK_COUNT; ++i) {
        context->rtos_diagnostics.stack_high_water[i] =
            task_handles[i] != 0
                ? (uint32_t)uxTaskGetStackHighWaterMark(task_handles[i])
                : 0u;
    }
    for (i = 0u; i < 15u; ++i) {
        UBaseType_t waiting = queues[i] != 0
            ? uxQueueMessagesWaiting(queues[i]) : 0u;

        context->rtos_diagnostics.queue_current[i] = (uint16_t)waiting;
        if (waiting > context->rtos_diagnostics.queue_high_water[i]) {
            context->rtos_diagnostics.queue_high_water[i] =
                (uint16_t)waiting;
        }
    }
    context->rtos_diagnostics.samples++;

    status_count = uxTaskGetSystemState(
        task_status, APP_RTOS_SYSTEM_TASK_CAPACITY, &total_runtime);
    if (status_count == 0u) {
        context->rtos_diagnostics.runtime_errors++;
        return;
    }
    for (i = 0u; i < status_count; ++i) {
        unsigned int task_index;

        if (strcmp(task_status[i].pcTaskName, "IDLE") == 0) {
            current_idle_runtime = (uint32_t)task_status[i].ulRunTimeCounter;
        }
        for (task_index = 0u; task_index < GATEWAY_TASK_COUNT;
             ++task_index) {
            if (task_status[i].xHandle == task_handles[task_index]) {
                current_runtime[task_index] =
                    (uint32_t)task_status[i].ulRunTimeCounter;
                break;
            }
        }
    }
    if (runtime_baseline_valid != 0u) {
        uint32_t total_delta = total_runtime - previous_total_runtime;
        uint32_t known_delta = 0u;
        uint32_t idle_delta =
            current_idle_runtime - previous_idle_runtime;

        if (total_delta == 0u) {
            context->rtos_diagnostics.runtime_errors++;
        } else {
            for (i = 0u; i < GATEWAY_TASK_COUNT; ++i) {
                uint32_t delta = current_runtime[i] - previous_runtime[i];
                uint32_t permille = (uint32_t)(
                    ((uint64_t)delta * 1000u + total_delta / 2u) /
                    total_delta);

                known_delta += delta;
                context->rtos_diagnostics.cpu_permille[i] =
                    (uint16_t)(permille > 1000u ? 1000u : permille);
            }
            context->rtos_diagnostics.idle_cpu_permille = (uint16_t)(
                ((uint64_t)idle_delta * 1000u + total_delta / 2u) /
                total_delta);
            if (context->rtos_diagnostics.idle_cpu_permille > 1000u) {
                context->rtos_diagnostics.idle_cpu_permille = 1000u;
            }
            context->rtos_diagnostics.system_cpu_permille =
                known_delta + idle_delta < total_delta
                    ? (uint16_t)(((uint64_t)(total_delta - known_delta -
                        idle_delta) * 1000u + total_delta / 2u) /
                        total_delta)
                    : 0u;
            context->rtos_diagnostics.runtime_samples++;
        }
    } else {
        runtime_baseline_valid = 1u;
    }
    for (i = 0u; i < GATEWAY_TASK_COUNT; ++i) {
        previous_runtime[i] = current_runtime[i];
    }
    previous_idle_runtime = current_idle_runtime;
    previous_total_runtime = total_runtime;
}

static const char *task_name_from_token(uint32_t task_token)
{
    unsigned int i;

    for (i = 0u; i < GATEWAY_TASK_COUNT; ++i) {
        if ((uint32_t)(uintptr_t)task_handles[i] == task_token) {
            return task_specs[i].name;
        }
    }
    return "unknown";
}

static const char *power_mode_name(power_mode_t mode)
{
    static const char *const names[POWER_MODE_COUNT] = {
        "active", "eco", "tickless", "stop", "standby"
    };

    return (unsigned int)mode < POWER_MODE_COUNT ? names[mode] : "invalid";
}

static const char *power_policy_name(power_policy_t policy)
{
    static const char *const names[] = { "auto", "active", "eco" };

    return (unsigned int)policy < sizeof(names) / sizeof(names[0])
        ? names[policy] : "invalid";
}

status_t app_rtos_start(app_context_t *context)
{
    status_t status;

    if (context == 0) {
        return ERR_INVALID_ARG;
    }
    app_context = context;
    memset(&channels, 0, sizeof(channels));
    memset(previous_runtime, 0, sizeof(previous_runtime));
    previous_idle_runtime = 0u;
    previous_total_runtime = 0u;
    runtime_baseline_valid = 0u;

    status = create_channels();
    if (status != SYS_OK) {
        return status;
    }
    status = create_tasks();
    if (status != SYS_OK) {
        return status;
    }

    if ((context->initialization_mask & APP_INITIALIZED_ACQUISITION) != 0u) {
        xEventGroupSetBits(channels.system_events,
                           SYSTEM_EVENT_ACQUISITION_READY);
    }
    if ((context->initialization_mask & APP_INITIALIZED_FIELDBUS) != 0u) {
        xEventGroupSetBits(channels.system_events,
                           SYSTEM_EVENT_FIELDBUS_READY);
    }
    if ((context->initialization_mask & APP_INITIALIZED_CONTROL) != 0u) {
        xEventGroupSetBits(channels.system_events,
                           SYSTEM_EVENT_CONTROL_READY);
    }
    if (context->storage_startup_status == SYS_OK) {
        xEventGroupSetBits(channels.system_events,
                           SYSTEM_EVENT_STORAGE_READY);
    }
    vTaskStartScheduler();
    return ERR_NO_MEMORY;
}

status_t app_rtos_submit_can_message(const gateway_can_tx_message_t *message)
{
    if (message == 0 || channels.can_tx == 0) {
        return ERR_INVALID_ARG;
    }
    return xQueueSend(channels.can_tx, message, 0U) == pdPASS
        ? SYS_OK : ERR_QUEUE_FULL;
}

status_t app_rtos_publish_measurement(const gateway_measurement_t *measurement)
{
    if (measurement == 0 || channels.measurement == 0) {
        return ERR_INVALID_ARG;
    }
    return xQueueSend(channels.measurement, measurement, 0U) == pdPASS
        ? SYS_OK
        : ERR_QUEUE_FULL;
}

status_t app_rtos_submit_ota_command(const gateway_ota_command_t *command)
{
    if (command == 0 || channels.ota_command == 0) {
        return ERR_INVALID_ARG;
    }
    return xQueueSend(channels.ota_command, command, 0U) == pdPASS
        ? SYS_OK
        : ERR_QUEUE_FULL;
}

status_t app_rtos_submit_config(
    const gateway_storage_config_request_t *request)
{
    if (request == 0 || channels.storage_config == 0) {
        return ERR_INVALID_ARG;
    }
    return xQueueSend(channels.storage_config, request,
                      pdMS_TO_TICKS(5u)) == pdPASS
        ? SYS_OK : ERR_QUEUE_FULL;
}

status_t app_rtos_submit_ui_page(ui_page_id_t page)
{
    if ((unsigned int)page >= UI_PAGE_COUNT || channels.ui_command == 0) {
        return ERR_INVALID_ARG;
    }
    if (channels.system_events == 0 ||
        (xEventGroupGetBits(channels.system_events) &
         SYSTEM_EVENT_UI_READY) == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    return xQueueSend(channels.ui_command, &page, 0u) == pdPASS
        ? SYS_OK : ERR_QUEUE_FULL;
}

status_t app_rtos_submit_network_control(
    const gateway_network_control_request_t *request)
{
    if (request == 0 || channels.network_control == 0) {
        return ERR_INVALID_ARG;
    }
    return xQueueSend(channels.network_control, request,
                      pdMS_TO_TICKS(20u)) == pdPASS
        ? SYS_OK : ERR_QUEUE_FULL;
}

static void supervisor_task(void *argument)
{
    app_context_t *context = argument;
    TickType_t next_wake = xTaskGetTickCount();
    uint32_t next_diagnostics_ms = 0u;
    uint8_t alarm_lock_held = 0u;
    uint8_t boot_fault_latched = 0u;
    const uint32_t required_services =
        APP_INITIALIZED_ACQUISITION | APP_INITIALIZED_FIELDBUS |
        APP_INITIALIZED_STORAGE | APP_INITIALIZED_CONTROL |
        APP_INITIALIZED_NETWORK | APP_INITIALIZED_CONFIG |
        APP_INITIALIZED_UI | APP_INITIALIZED_CLI |
        APP_INITIALIZED_RELIABILITY;

    if (supervisor_subsystem_start(
            &context->supervisor,
            (uint32_t)(next_wake * portTICK_PERIOD_MS),
            context->heartbeat) != SYS_OK) {
        supervisor_subsystem_latch_fault(&context->supervisor,
                                         ERR_DEVICE_NOT_READY);
    }

    for (;;) {
        uint32_t now_ms =
            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        EventBits_t bits = xEventGroupGetBits(channels.system_events);
        uint8_t alarm_active =
            (bits & SYSTEM_EVENT_ALARM_ACTIVE) != 0u ? 1u : 0u;
        status_t status;
        power_command_t power_command;

        while (xQueueReceive(channels.power_command, &power_command, 0u) ==
               pdPASS) {
            if (power_command.operation == POWER_COMMAND_REQUEST_STOP) {
                status = deep_power_controller_request(
                    &context->deep_power, POWER_STOP_PERIODIC,
                    power_command.duration_ms, DEEP_POWER_CONFIRMATION);
            } else if (power_command.operation ==
                       POWER_COMMAND_REQUEST_STANDBY) {
                status = deep_power_controller_request(
                    &context->deep_power, POWER_STANDBY_SHIPPING, 0u,
                    DEEP_POWER_CONFIRMATION);
            } else {
                status = deep_power_controller_cancel(&context->deep_power);
                xEventGroupClearBits(
                    channels.system_events,
                    SYSTEM_EVENT_POWER_QUIESCE_REQUEST);
            }
            if (status != SYS_OK && status != ERR_UNSUPPORTED &&
                status != ERR_DEVICE_NOT_READY) {
                supervisor_subsystem_latch_fault(&context->supervisor,
                                                 status);
            }
        }

        if (alarm_active != 0u && alarm_lock_held == 0u) {
            if (power_lock_acquire(context, PM_LOCK_ALARM_ACTIVE) == SYS_OK) {
                alarm_lock_held = 1u;
            }
        } else if (alarm_active == 0u && alarm_lock_held != 0u) {
            if (power_lock_release(context, PM_LOCK_ALARM_ACTIVE) == SYS_OK) {
                alarm_lock_held = 0u;
            }
        }
        (void)power_manager_evaluate(&context->power, now_ms, alarm_active);
        if (context->deep_power.health.request_pending != 0u) {
            deep_power_health_t deep_health;

            xEventGroupSetBits(channels.system_events,
                               SYSTEM_EVENT_POWER_QUIESCE_REQUEST);
            bits = xEventGroupGetBits(channels.system_events);
            status = deep_power_controller_process(
                &context->deep_power, deep_power_quiesced_mask(bits),
                power_manager_deepest_allowed(&context->power),
                watchdog_device_remaining_ms(&context->watchdog));
            (void)deep_power_controller_get_health(&context->deep_power,
                                                   &deep_health);
            if (deep_health.request_pending == 0u) {
                xEventGroupClearBits(channels.system_events,
                    SYSTEM_EVENT_POWER_QUIESCE_REQUEST);
            } else if (status != SYS_OK &&
                       status != ERR_DEVICE_NOT_READY &&
                       status != ERR_TIMEOUT) {
                (void)deep_power_controller_cancel(&context->deep_power);
                xEventGroupClearBits(channels.system_events,
                    SYSTEM_EVENT_POWER_QUIESCE_REQUEST);
            }
        } else if ((bits & SYSTEM_EVENT_POWER_QUIESCE_REQUEST) != 0u) {
            xEventGroupClearBits(channels.system_events,
                SYSTEM_EVENT_POWER_QUIESCE_REQUEST);
        }
        status = supervisor_subsystem_process(
            &context->supervisor, now_ms, context->heartbeat,
            (context->initialization_mask & required_services) ==
                    required_services
                ? 1u : 0u);
        if (status == ERR_TIMEOUT) {
            xEventGroupSetBits(channels.system_events,
                               SYSTEM_EVENT_FAULT_ACTIVE);
        }
        if (context->boot_confirmation.attempted == 0u &&
            context->boot_confirmation_queued == 0u &&
            supervisor_subsystem_boot_confirm_ready(
                &context->supervisor, now_ms) != 0u) {
            gateway_ota_command_t command;

            command.type = GATEWAY_OTA_COMMAND_CONFIRM_BOOT;
            command.request_id = now_ms;
            if (xQueueSend(channels.ota_command, &command, 0u) == pdPASS) {
                context->boot_confirmation_queued = 1u;
            }
        }
        if (context->boot_confirmation_queued == 0u &&
            context->boot_confirmation.attempted != 0u &&
            context->boot_confirmation.health.confirmed == 0u &&
            context->boot_confirmation.health.last_error != SYS_OK &&
            boot_fault_latched == 0u) {
            supervisor_subsystem_latch_fault(
                &context->supervisor,
                context->boot_confirmation.health.last_error);
            xEventGroupSetBits(channels.system_events,
                               SYSTEM_EVENT_FAULT_ACTIVE);
            boot_fault_latched = 1u;
        }
        if (now_ms - next_diagnostics_ms >= 1000u) {
            collect_rtos_diagnostics(context);
            next_diagnostics_ms = now_ms;
        }
        app_context_mark_alive(context, GATEWAY_TASK_SUPERVISOR);
        vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(100U));
    }
}

static void acquisition_task(void *argument)
{
    app_context_t *context = argument;
    TickType_t next_wake = xTaskGetTickCount();
    const TickType_t period_ticks = pdMS_TO_TICKS(100u);
    const uint32_t period_ms =
        (uint32_t)(period_ticks * portTICK_PERIOD_MS);
    gateway_measurement_t measurements[ACQUISITION_MEASUREMENT_COUNT];
    uint8_t power_suspended = 0u;

    (void)periodic_timing_monitor_construct(
        &context->acquisition_timing, period_ms,
        (uint32_t)(2u * portTICK_PERIOD_MS));
    for (;;) {
        TickType_t actual_release = xTaskGetTickCount();
        uint32_t now_ms =
            (uint32_t)(actual_release * portTICK_PERIOD_MS);
        EventBits_t power_bits = xEventGroupGetBits(channels.system_events);

        if ((power_bits & SYSTEM_EVENT_POWER_QUIESCE_REQUEST) != 0u) {
            if (power_suspended == 0u) {
                status_t status =
                    (context->initialization_mask &
                     APP_INITIALIZED_ACQUISITION) != 0u
                        ? acquisition_subsystem_suspend(
                              &context->acquisition)
                        : SYS_OK;

                if (status == SYS_OK) {
                    power_suspended = 1u;
                    xEventGroupSetBits(channels.system_events,
                        SYSTEM_EVENT_POWER_ACK_ACQUISITION);
                } else {
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_FAULT_ACTIVE);
                }
            }
            app_context_mark_alive(context, GATEWAY_TASK_ACQUISITION);
            vTaskDelay(pdMS_TO_TICKS(20u));
            continue;
        }
        if (power_suspended != 0u) {
            status_t status =
                (context->initialization_mask &
                 APP_INITIALIZED_ACQUISITION) != 0u
                    ? acquisition_subsystem_resume(&context->acquisition)
                    : SYS_OK;

            xEventGroupClearBits(channels.system_events,
                SYSTEM_EVENT_POWER_ACK_ACQUISITION);
            power_suspended = 0u;
            next_wake = actual_release;
            if (status != SYS_OK) {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
        }

        (void)periodic_timing_monitor_note(
            &context->acquisition_timing, now_ms,
            (uint32_t)(next_wake * portTICK_PERIOD_MS));
        if ((context->initialization_mask & APP_INITIALIZED_ACQUISITION) != 0u) {
            size_t count = 0u;
            size_t i;

            (void)acquisition_subsystem_process(
                &context->acquisition, now_ms,
                measurements, ACQUISITION_MEASUREMENT_COUNT, &count);
            for (i = 0u; i < count; ++i) {
                if (app_rtos_publish_measurement(&measurements[i]) != SYS_OK) {
                    context->measurement_publish_drops++;
                }
            }
        }
        app_context_mark_alive(context, GATEWAY_TASK_ACQUISITION);
        vTaskDelayUntil(&next_wake, period_ticks);
    }
}

static void data_hub_task(void *argument)
{
    app_context_t *context = argument;
    gateway_measurement_t measurement;

    for (;;) {
        if (xQueueReceive(channels.measurement, &measurement,
                          pdMS_TO_TICKS(1000U)) == pdPASS) {
            gateway_alarm_event_t alarm_events[
                ALARM_MAX_EVENTS_PER_MEASUREMENT];
            gateway_can_tx_message_t can_message;
            gateway_network_event_t network_event;
            gateway_storage_log_request_t storage_log;
            relay_state_t relay_state = RELAY_DEENERGIZED;
            size_t alarm_count = 0u;
            uint32_t active_alarm_count = 0u;
            size_t i;
            status_t alarm_status;

            if (xSemaphoreTake(channels.config_mutex,
                               portMAX_DELAY) == pdPASS) {
                alarm_status = alarm_subsystem_process(
                    &context->alarm, &measurement, alarm_events,
                    ALARM_MAX_EVENTS_PER_MEASUREMENT, &alarm_count);
                active_alarm_count =
                    alarm_subsystem_active_count(&context->alarm);
                (void)relay_get_state(&context->relay, &relay_state);
                xSemaphoreGive(channels.config_mutex);
            } else {
                alarm_status = ERR_TIMEOUT;
                active_alarm_count = context->snapshot.active_alarm_count;
            }
            if (alarm_status != SYS_OK &&
                alarm_status != ERR_UNSUPPORTED) {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
            if (xSemaphoreTake(channels.snapshot_mutex,
                               pdMS_TO_TICKS(10U)) == pdPASS) {
                context->snapshot.sequence++;
                context->snapshot.latest = measurement;
                context->snapshot.active_alarm_count = active_alarm_count;
                context->snapshot.system_flags =
                    (uint32_t)xEventGroupGetBits(channels.system_events);
                context->snapshot.relay_energized =
                    relay_state == RELAY_ENERGIZED ? 1u : 0u;
                xQueueOverwrite(channels.ui_snapshot, &context->snapshot);
                xSemaphoreGive(channels.snapshot_mutex);
            }

            if (active_alarm_count != 0u) {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_ALARM_ACTIVE);
            } else if (alarm_status == SYS_OK ||
                       alarm_status == ERR_UNSUPPORTED) {
                xEventGroupClearBits(channels.system_events,
                                     SYSTEM_EVENT_ALARM_ACTIVE);
            }

            memset(&network_event, 0, sizeof(network_event));
            network_event.type = GATEWAY_NETWORK_TELEMETRY;
            network_event.sequence = context->snapshot.sequence;
            network_event.qos = 1u;
            network_event.payload.measurement = measurement;
            xQueueOverwrite(channels.network_telemetry, &network_event);

            storage_log.sequence = context->snapshot.sequence;
            storage_log.measurement = measurement;
            if (xQueueSend(channels.storage_log, &storage_log, 0U) !=
                pdPASS) {
                context->storage_log_publish_drops++;
            }

            for (i = 0u; i < alarm_count; ++i) {
                gateway_storage_alarm_request_t storage_alarm;

                storage_alarm.event = alarm_events[i];
                if (xQueueSend(channels.storage_alarm, &storage_alarm,
                               pdMS_TO_TICKS(5u)) != pdPASS) {
                    context->storage_alarm_publish_drops++;
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_FAULT_ACTIVE);
                }
                memset(&network_event, 0, sizeof(network_event));
                network_event.type = GATEWAY_NETWORK_ALARM;
                network_event.sequence = alarm_events[i].event_id;
                network_event.qos = 1u;
                network_event.payload.alarm = alarm_events[i];
                if (xQueueSend(channels.network_alarm, &network_event,
                               pdMS_TO_TICKS(5u)) !=
                    pdPASS) {
                    context->network_alarm_publish_drops++;
                }
            }

            if (measurement.source != GATEWAY_SOURCE_CAN) {
                can_message.request_id = context->snapshot.sequence;
                can_message.measurement = measurement;
                if (app_rtos_submit_can_message(&can_message) != SYS_OK) {
                    context->can_tx_publish_drops++;
                }
            }
        }
        app_context_mark_alive(context, GATEWAY_TASK_DATA_HUB);
    }
}

static void modbus_task(void *argument)
{
    app_context_t *context = argument;
    TickType_t next_wake;
    uint8_t power_suspended = 0u;

    (void)xEventGroupWaitBits(channels.system_events,
                              SYSTEM_EVENT_FIELDBUS_READY,
                              pdFALSE, pdTRUE, portMAX_DELAY);
    next_wake = xTaskGetTickCount();

    for (;;) {
        gateway_measurement_t measurement;
        EventBits_t power_bits = xEventGroupGetBits(channels.system_events);

        if ((power_bits & SYSTEM_EVENT_POWER_QUIESCE_REQUEST) != 0u) {
            if (power_suspended == 0u) {
                status_t status = rs485_bus_suspend(
                    &context->modbus_rs485);

                if (status == SYS_OK) {
                    power_suspended = 1u;
                    xEventGroupSetBits(channels.system_events,
                        SYSTEM_EVENT_POWER_ACK_MODBUS);
                } else {
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_FAULT_ACTIVE);
                }
            }
            app_context_mark_alive(context, GATEWAY_TASK_MODBUS);
            vTaskDelay(pdMS_TO_TICKS(20u));
            continue;
        }
        if (power_suspended != 0u) {
            status_t status = rs485_bus_resume(&context->modbus_rs485);

            xEventGroupClearBits(channels.system_events,
                                 SYSTEM_EVENT_POWER_ACK_MODBUS);
            power_suspended = 0u;
            next_wake = xTaskGetTickCount();
            if (status != SYS_OK) {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
        }

        if (power_lock_acquire(context,
                               PM_LOCK_MODBUS_TRANSACTION) == SYS_OK) {
            (void)fieldbus_subsystem_poll_modbus(
                &context->fieldbus,
                (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                &measurement);
            (void)power_lock_release(context,
                                     PM_LOCK_MODBUS_TRANSACTION);
        } else {
            memset(&measurement, 0, sizeof(measurement));
            measurement.source = GATEWAY_SOURCE_MODBUS;
            measurement.quality = GATEWAY_QUALITY_UNAVAILABLE;
            measurement.error = ERR_DEVICE_NOT_READY;
        }
        if (app_rtos_publish_measurement(&measurement) != SYS_OK) {
            context->measurement_publish_drops++;
        }
        app_context_mark_alive(context, GATEWAY_TASK_MODBUS);
        vTaskDelayUntil(&next_wake, pdMS_TO_TICKS(1000U));
    }
}

static void can_task(void *argument)
{
    app_context_t *context = argument;
    uint8_t power_suspended = 0u;
    uint8_t can_lock_held = 0u;

    (void)xEventGroupWaitBits(channels.system_events,
                              SYSTEM_EVENT_FIELDBUS_READY,
                              pdFALSE, pdTRUE, portMAX_DELAY);
    if (power_lock_acquire(context, PM_LOCK_CAN_MONITORING) != SYS_OK) {
        xEventGroupSetBits(channels.system_events,
                           SYSTEM_EVENT_FAULT_ACTIVE);
    } else {
        can_lock_held = 1u;
    }

    for (;;) {
        gateway_measurement_t measurements[4];
        gateway_can_tx_message_t message;
        uint32_t event_bits = CAN_BUS_EVENT_NONE;
        size_t count = 0u;
        size_t i;
        status_t wait_status;
        EventBits_t power_bits = xEventGroupGetBits(channels.system_events);

        if ((power_bits & SYSTEM_EVENT_POWER_QUIESCE_REQUEST) != 0u) {
            if (power_suspended == 0u) {
                status_t status = can_bus_suspend(&context->can_bus);

                if (status == SYS_OK) {
                    if (can_lock_held != 0u) {
                        (void)power_lock_release(
                            context, PM_LOCK_CAN_MONITORING);
                        can_lock_held = 0u;
                    }
                    power_suspended = 1u;
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_POWER_ACK_CAN);
                } else {
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_FAULT_ACTIVE);
                }
            }
            app_context_mark_alive(context, GATEWAY_TASK_CAN);
            vTaskDelay(pdMS_TO_TICKS(20u));
            continue;
        }
        if (power_suspended != 0u) {
            status_t status = can_bus_resume(&context->can_bus);

            if (status == SYS_OK &&
                power_lock_acquire(context, PM_LOCK_CAN_MONITORING) ==
                    SYS_OK) {
                can_lock_held = 1u;
            } else {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
            xEventGroupClearBits(channels.system_events,
                                 SYSTEM_EVENT_POWER_ACK_CAN);
            power_suspended = 0u;
        }

        wait_status = fieldbus_subsystem_wait_can(&context->fieldbus, 20u,
                                                  &event_bits);
        if (wait_status == ERR_DEVICE_NOT_READY) {
            vTaskDelay(pdMS_TO_TICKS(20u));
        }
        (void)fieldbus_subsystem_process_can(
            &context->fieldbus, event_bits,
            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
            measurements, 4u, &count);
        for (i = 0u; i < count; ++i) {
            if (app_rtos_publish_measurement(&measurements[i]) != SYS_OK) {
                context->measurement_publish_drops++;
            }
        }
        while (xQueueReceive(channels.can_tx, &message, 0U) == pdPASS) {
            (void)fieldbus_subsystem_send_can(&context->fieldbus,
                                              &message.measurement);
        }
        app_context_mark_alive(context, GATEWAY_TASK_CAN);
    }
}

static void notify_ota_waiter(TaskHandle_t waiter)
{
    if (waiter != 0) {
        (void)xTaskNotify(waiter, OTA_IO_NOTIFY_DONE, eSetBits);
    }
}

static void process_ota_network_request(app_context_t *context,
                                        ota_network_request_t *request)
{
    if (request == 0) {
        return;
    }
    request->length = 0u;
    request->content_length = 0u;
    if (context->network.health.ota_lease_active == 0u) {
        request->status = ERR_DEVICE_NOT_READY;
    } else {
        if (request->operation != OTA_NETWORK_CLOSE_PACKAGE &&
            context->network_transport.initialized == 0u) {
            request->status = network_transport_init(
                &context->network_transport);
            if (request->status != SYS_OK) {
                notify_ota_waiter(request->waiter);
                return;
            }
        }
        switch (request->operation) {
        case OTA_NETWORK_FETCH_MANIFEST:
            request->status = http_get_document(
                &context->ota_http, request->url, request->buffer,
                request->capacity, &request->length);
            break;
        case OTA_NETWORK_OPEN_PACKAGE:
            request->status = http_open_get(&context->ota_http,
                                            request->url);
            if (request->status == SYS_OK) {
                request->content_length =
                    context->ota_http.header.content_length;
            }
            break;
        case OTA_NETWORK_READ_PACKAGE:
            request->status = http_read_body_chunk(
                &context->ota_http, request->buffer, request->capacity,
                &request->length);
            break;
        case OTA_NETWORK_CLOSE_PACKAGE:
            request->status = http_close(&context->ota_http);
            break;
        default:
            request->status = ERR_UNSUPPORTED;
            break;
        }
    }
    notify_ota_waiter(request->waiter);
}

static void network_task(void *argument)
{
    app_context_t *context = argument;
    gateway_network_event_t event;
    network_health_t health;
    const BaseType_t configured =
        (context->initialization_mask & APP_INITIALIZED_NETWORK) != 0u
            ? pdTRUE : pdFALSE;
    uint8_t ota_lock_held = 0u;
    uint8_t power_suspended = 0u;
    status_t http_status = ERR_DEVICE_NOT_READY;

    if (configured == pdTRUE) {
        http_status = http_client_construct(
            &context->ota_http, &context->network_transport,
            context->ota_http_timeout_ms);
        context->network_startup_status = network_subsystem_start(
            &context->network,
            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
    }

    for (;;) {
        gateway_network_control_request_t control;
        uint32_t now_ms =
            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        EventBits_t power_bits = xEventGroupGetBits(channels.system_events);

        if ((power_bits & SYSTEM_EVENT_POWER_QUIESCE_REQUEST) != 0u) {
            if (power_suspended == 0u) {
                status_t status = configured == pdTRUE
                    ? network_subsystem_suspend(&context->network, now_ms)
                    : SYS_OK;

                if (status == SYS_OK) {
                    power_suspended = 1u;
                    xEventGroupClearBits(channels.system_events,
                        SYSTEM_EVENT_NETWORK_UP | SYSTEM_EVENT_MQTT_READY);
                    xEventGroupSetBits(channels.system_events,
                        SYSTEM_EVENT_POWER_ACK_NETWORK);
                } else if (status != ERR_DEVICE_NOT_READY) {
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_FAULT_ACTIVE);
                }
            }
            app_context_mark_alive(context, GATEWAY_TASK_NETWORK);
            vTaskDelay(pdMS_TO_TICKS(20u));
            continue;
        }
        if (power_suspended != 0u) {
            status_t status = configured == pdTRUE
                ? network_subsystem_resume(&context->network, now_ms)
                : SYS_OK;

            xEventGroupClearBits(channels.system_events,
                                 SYSTEM_EVENT_POWER_ACK_NETWORK);
            power_suspended = 0u;
            if (status != SYS_OK) {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
        }

        while (xQueueReceive(channels.network_control, &control, 0u) ==
               pdPASS) {
            gateway_network_control_result_t result;

            result.type = control.type;
            result.request_id = control.request_id;
            if (configured != pdTRUE) {
                result.status = ERR_DEVICE_NOT_READY;
            } else if (control.type == GATEWAY_NETWORK_CONTROL_OTA_ACQUIRE) {
                result.status = network_subsystem_acquire_ota_lease(
                    &context->network, now_ms);
            } else if (control.type ==
                       GATEWAY_NETWORK_CONTROL_OTA_RELEASE) {
                result.status = network_subsystem_release_ota_lease(
                    &context->network, now_ms);
            } else {
                result.status = ERR_UNSUPPORTED;
            }
            if (xQueueSend(channels.network_control_result, &result,
                           pdMS_TO_TICKS(20u)) != pdPASS) {
                context->network_control_publish_drops++;
            }
        }
        {
            ota_network_request_t *request = 0;

            while (xQueueReceive(channels.ota_network_request, &request,
                                 0u) == pdPASS) {
                if (http_status != SYS_OK && request != 0) {
                    request->status = http_status;
                    notify_ota_waiter(request->waiter);
                } else {
                    process_ota_network_request(context, request);
                }
                app_context_mark_alive(context, GATEWAY_TASK_NETWORK);
            }
        }
        while (xQueueReceive(channels.network_alarm, &event, 0u) == pdPASS) {
            if (configured != pdTRUE ||
                network_subsystem_submit(&context->network, &event) !=
                SYS_OK) {
                context->network_alarm_publish_drops++;
            }
        }
        if (xQueueReceive(channels.network_telemetry, &event, 0u) == pdPASS) {
            if (configured == pdTRUE) {
                (void)network_subsystem_submit(&context->network, &event);
            }
        }
        if (configured == pdTRUE) {
            status_t process_status;

            if (power_lock_acquire(context, PM_LOCK_NETWORK_TX) == SYS_OK) {
                process_status = network_subsystem_process(
                    &context->network, now_ms);
                (void)power_lock_release(context, PM_LOCK_NETWORK_TX);
            } else {
                process_status = ERR_DEVICE_NOT_READY;
            }

            if (network_subsystem_get_health(&context->network, &health) ==
                SYS_OK) {
                context->network_startup_status = health.mqtt_ready != 0u
                    ? SYS_OK : process_status;
                if (health.mqtt_ready != 0u) {
                    xEventGroupSetBits(channels.system_events,
                        SYSTEM_EVENT_NETWORK_UP | SYSTEM_EVENT_MQTT_READY);
                } else {
                    xEventGroupClearBits(channels.system_events,
                        SYSTEM_EVENT_NETWORK_UP | SYSTEM_EVENT_MQTT_READY);
                }
                if (health.ota_lease_active != 0u) {
                    if (ota_lock_held == 0u &&
                        power_lock_acquire(context, PM_LOCK_OTA) == SYS_OK) {
                        ota_lock_held = 1u;
                    }
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_OTA_ACTIVE);
                } else {
                    if (ota_lock_held != 0u &&
                        power_lock_release(context, PM_LOCK_OTA) == SYS_OK) {
                        ota_lock_held = 0u;
                    }
                    xEventGroupClearBits(channels.system_events,
                                         SYSTEM_EVENT_OTA_ACTIVE);
                }
            }
        }
        app_context_mark_alive(context, GATEWAY_TASK_NETWORK);
        vTaskDelay(pdMS_TO_TICKS(20u));
    }
}

static status_t wait_for_network_control_result(
    const gateway_network_control_request_t *request,
    TickType_t timeout_ticks)
{
    TickType_t start = xTaskGetTickCount();
    TickType_t remaining = timeout_ticks;

    for (;;) {
        gateway_network_control_result_t result;

        if (xQueueReceive(channels.network_control_result, &result,
                          remaining) != pdPASS) {
            return ERR_TIMEOUT;
        }
        if (result.request_id == request->request_id &&
            result.type == request->type) {
            return result.status;
        }
        {
            TickType_t elapsed = xTaskGetTickCount() - start;

            if (elapsed >= timeout_ticks) {
                return ERR_TIMEOUT;
            }
            remaining = timeout_ticks - elapsed;
        }
    }
}

static status_t process_boot_confirmation(app_context_t *context)
{
    status_t status = ERR_DEVICE_NOT_READY;

    if (context->supervisor.health.healthy != 0u &&
        power_lock_acquire(context, PM_LOCK_FLASH_WRITE) == SYS_OK) {
        status = boot_confirmation_confirm(&context->boot_confirmation);
        (void)power_lock_release(context, PM_LOCK_FLASH_WRITE);
    }
    context->boot_confirmation_queued = 0u;
    if (status != SYS_OK && status != ERR_DEVICE_NOT_READY) {
        xEventGroupSetBits(channels.system_events,
                           SYSTEM_EVENT_FAULT_ACTIVE);
    }
    return status;
}

static void ota_poll_cancel_commands(ota_rtos_port_t *port)
{
    gateway_ota_command_t command;

    while (xQueueReceive(channels.ota_command, &command, 0u) == pdPASS) {
        if (command.type == GATEWAY_OTA_COMMAND_CANCEL) {
            port->cancel_seen = 1u;
            if (port->context->ota.initialized != 0u) {
                (void)ota_manager_abort(&port->context->ota);
            }
        } else if (command.type == GATEWAY_OTA_COMMAND_CONFIRM_BOOT) {
            (void)process_boot_confirmation(port->context);
        }
    }
}

static void clear_ota_notification(void)
{
    uint32_t ignored;

    (void)xTaskNotifyWait(0u, UINT32_MAX, &ignored, 0u);
}

static status_t wait_for_ota_io(ota_rtos_port_t *port, status_t *result)
{
    for (;;) {
        uint32_t events = 0u;

        if (xTaskNotifyWait(0u, UINT32_MAX, &events,
                            pdMS_TO_TICKS(OTA_IO_WAIT_SLICE_MS)) == pdTRUE &&
            (events & OTA_IO_NOTIFY_DONE) != 0u) {
            ota_poll_cancel_commands(port);
            return port->cancel_seen != 0u ? ERR_OTA_ABORTED : *result;
        }
        ota_poll_cancel_commands(port);
        app_context_mark_alive(port->context, GATEWAY_TASK_OTA);
    }
}

static status_t submit_ota_network_request(ota_rtos_port_t *port,
                                           ota_network_request_t *request)
{
    ota_network_request_t *queued = request;

    request->waiter = xTaskGetCurrentTaskHandle();
    request->status = ERR_DEVICE_NOT_READY;
    clear_ota_notification();
    if (xQueueSend(channels.ota_network_request, &queued,
                   pdMS_TO_TICKS(100u)) != pdPASS) {
        return ERR_QUEUE_FULL;
    }
    return wait_for_ota_io(port, &request->status);
}

static status_t submit_ota_storage_request(ota_rtos_port_t *port,
                                           ota_storage_request_t *request)
{
    ota_storage_request_t *queued = request;

    request->waiter = xTaskGetCurrentTaskHandle();
    request->status = ERR_DEVICE_NOT_READY;
    clear_ota_notification();
    if (xQueueSend(channels.ota_storage_request, &queued,
                   pdMS_TO_TICKS(100u)) != pdPASS) {
        return ERR_QUEUE_FULL;
    }
    return wait_for_ota_io(port, &request->status);
}

static status_t ota_port_fetch_manifest(void *opaque, const char *url,
                                        uint8_t *buffer, size_t capacity,
                                        size_t *out_length)
{
    ota_rtos_port_t *port = opaque;
    ota_network_request_t request;
    status_t status;

    memset(&request, 0, sizeof(request));
    request.operation = OTA_NETWORK_FETCH_MANIFEST;
    request.url = url;
    request.buffer = buffer;
    request.capacity = capacity;
    status = submit_ota_network_request(port, &request);
    if (out_length != 0) {
        *out_length = status == SYS_OK ? request.length : 0u;
    }
    return status;
}

static status_t ota_port_http_open(void *opaque, const char *url,
                                   uint32_t *out_content_length)
{
    ota_rtos_port_t *port = opaque;
    ota_network_request_t request;
    status_t status;

    memset(&request, 0, sizeof(request));
    request.operation = OTA_NETWORK_OPEN_PACKAGE;
    request.url = url;
    status = submit_ota_network_request(port, &request);
    if (out_content_length != 0) {
        *out_content_length = status == SYS_OK
            ? request.content_length : 0u;
    }
    return status;
}

static status_t ota_port_http_read(void *opaque, uint8_t *buffer,
                                   size_t capacity, size_t *out_length)
{
    ota_rtos_port_t *port = opaque;
    ota_network_request_t request;
    status_t status;

    memset(&request, 0, sizeof(request));
    request.operation = OTA_NETWORK_READ_PACKAGE;
    request.buffer = buffer;
    request.capacity = capacity;
    status = submit_ota_network_request(port, &request);
    if (out_length != 0) {
        *out_length = status == SYS_OK ? request.length : 0u;
    }
    return status;
}

static status_t ota_port_http_close(void *opaque)
{
    ota_rtos_port_t *port = opaque;
    ota_network_request_t request;

    memset(&request, 0, sizeof(request));
    request.operation = OTA_NETWORK_CLOSE_PACKAGE;
    return submit_ota_network_request(port, &request);
}

static status_t ota_port_staging_begin(void *opaque, size_t package_size)
{
    ota_rtos_port_t *port = opaque;
    ota_storage_request_t request;
    status_t status;

    memset(&request, 0, sizeof(request));
    request.operation = OTA_STORAGE_BEGIN;
    request.length = package_size;
    status = submit_ota_storage_request(port, &request);
    while (status == SYS_OK && request.complete == 0u) {
        memset(&request, 0, sizeof(request));
        request.operation = OTA_STORAGE_ERASE_NEXT;
        status = submit_ota_storage_request(port, &request);
    }
    return status;
}

static status_t ota_port_staging_write(void *opaque, uint32_t offset,
                                       const uint8_t *data, size_t length)
{
    ota_rtos_port_t *port = opaque;
    ota_storage_request_t request;

    memset(&request, 0, sizeof(request));
    request.operation = OTA_STORAGE_WRITE;
    request.offset = offset;
    request.data = data;
    request.length = length;
    return submit_ota_storage_request(port, &request);
}

static status_t ota_port_metadata_load(void *opaque,
                                       boot_metadata_t *out_metadata,
                                       app_slot_t *out_copy_slot)
{
    ota_rtos_port_t *port = opaque;

    return boot_meta_load(&port->context->boot_confirmation.store,
                          out_metadata, out_copy_slot);
}

static status_t ota_port_metadata_commit(
    void *opaque, const boot_metadata_t *current,
    app_slot_t current_copy_slot, const boot_metadata_t *desired,
    boot_metadata_t *out_committed, app_slot_t *out_copy_slot)
{
    ota_rtos_port_t *port = opaque;
    status_t status;

    status = power_lock_acquire(port->context, PM_LOCK_FLASH_WRITE);
    if (status != SYS_OK) {
        return status;
    }
    status = boot_meta_commit(&port->context->boot_confirmation.store,
        current, current_copy_slot, desired, out_committed, out_copy_slot);
    (void)power_lock_release(port->context, PM_LOCK_FLASH_WRITE);
    return status;
}

static status_t ota_port_staging_metadata_commit(void *opaque,
                                                  const uint8_t *record,
                                                  size_t record_size)
{
    ota_rtos_port_t *port = opaque;
    ota_storage_request_t request;

    memset(&request, 0, sizeof(request));
    request.operation = OTA_STORAGE_COMMIT_METADATA;
    request.data = record;
    request.length = record_size;
    return submit_ota_storage_request(port, &request);
}

static void ota_port_enter_critical(void *opaque)
{
    ota_rtos_port_t *port = opaque;

    app_critical_enter(port != 0 ? port->context : 0);
}

static void ota_port_exit_critical(void *opaque)
{
    ota_rtos_port_t *port = opaque;

    app_critical_exit(port != 0 ? port->context : 0);
}

static status_t prepare_ota_manager(app_context_t *context)
{
    ota_manager_port_t port;
    ota_manager_config_t config;
    boot_metadata_t metadata;
    app_slot_t copy_slot;
    status_t status;

    status = boot_meta_load(&context->boot_confirmation.store,
                            &metadata, &copy_slot);
    if (status != SYS_OK) {
        return status;
    }
    (void)copy_slot;
    memset(&port, 0, sizeof(port));
    ota_rtos_port.context = context;
    ota_rtos_port.cancel_seen = 0u;
    port.fetch_manifest = ota_port_fetch_manifest;
    port.http_open = ota_port_http_open;
    port.http_read = ota_port_http_read;
    port.http_close = ota_port_http_close;
    port.staging_begin = ota_port_staging_begin;
    port.staging_write = ota_port_staging_write;
    port.metadata_load = ota_port_metadata_load;
    port.metadata_commit = ota_port_metadata_commit;
    port.ota_metadata_commit = ota_port_staging_metadata_commit;
    port.enter_critical = ota_port_enter_critical;
    port.exit_critical = ota_port_exit_critical;
    port.context = &ota_rtos_port;

    config.manifest_url = context->ota_manifest_url;
    config.target_id = PROJECT_TARGET_ID;
    config.current_version = metadata.active_version;
    config.current_bootloader_version = BOOTLOADER_VERSION;
    return ota_manager_construct(&context->ota, &port, &config);
}

static status_t set_ota_network_lease(gateway_network_control_type_t type,
                                      uint32_t request_id)
{
    gateway_network_control_request_t request;
    status_t status;

    request.type = type;
    request.request_id = request_id;
    status = app_rtos_submit_network_control(&request);
    if (status == SYS_OK) {
        status = wait_for_network_control_result(
            &request, pdMS_TO_TICKS(2000u));
    }
    return status;
}

static status_t run_ota_network_phase(app_context_t *context,
                                      const gateway_ota_command_t *command)
{
    status_t status;
    status_t release_status;

    status = set_ota_network_lease(GATEWAY_NETWORK_CONTROL_OTA_ACQUIRE,
                                   command->request_id);
    if (status != SYS_OK) {
        return status;
    }
    status = prepare_ota_manager(context);
    if (status == SYS_OK) {
        status = ota_manager_check(&context->ota);
    }
    if (status == SYS_OK && command->type == GATEWAY_OTA_COMMAND_START) {
        status = ota_manager_download(&context->ota);
    }
    release_status = set_ota_network_lease(
        GATEWAY_NETWORK_CONTROL_OTA_RELEASE, command->request_id);
    if (status == SYS_OK && release_status != SYS_OK) {
        status = release_status;
    }
    return status;
}

static void ota_task(void *argument)
{
    app_context_t *context = argument;
    gateway_ota_command_t command;

    for (;;) {
        if (xQueueReceive(channels.ota_command, &command,
                          pdMS_TO_TICKS(1000U)) == pdPASS) {
            if (command.type == GATEWAY_OTA_COMMAND_CONFIRM_BOOT) {
                (void)process_boot_confirmation(context);
            } else if (command.type == GATEWAY_OTA_COMMAND_CHECK ||
                       command.type == GATEWAY_OTA_COMMAND_START) {
                status_t status = run_ota_network_phase(context, &command);

                if (status != SYS_OK && status != ERR_OTA_ABORTED) {
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_FAULT_ACTIVE);
                }
            } else if (command.type == GATEWAY_OTA_COMMAND_APPLY) {
                status_t status = context->ota.initialized != 0u
                    ? ota_manager_commit_pending(&context->ota)
                    : ERR_DEVICE_NOT_READY;

                if (status != SYS_OK) {
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_FAULT_ACTIVE);
                }
            } else if (command.type == GATEWAY_OTA_COMMAND_CANCEL) {
                if (context->ota.initialized != 0u) {
                    (void)ota_manager_abort(&context->ota);
                }
                (void)set_ota_network_lease(
                    GATEWAY_NETWORK_CONTROL_OTA_RELEASE,
                    command.request_id);
            }
        }
        app_context_mark_alive(context, GATEWAY_TASK_OTA);
    }
}

static void process_ota_storage_request(app_context_t *context,
                                        ota_storage_request_t *request)
{
    uint8_t needs_flash_lock;

    if (request == 0) {
        return;
    }
    request->complete = 0u;
    needs_flash_lock = request->operation != OTA_STORAGE_BEGIN ? 1u : 0u;
    if (context->storage.health.mounted == 0u) {
        request->status = ERR_DEVICE_NOT_READY;
    } else if (needs_flash_lock != 0u &&
               power_lock_acquire(context, PM_LOCK_FLASH_WRITE) != SYS_OK) {
        request->status = ERR_DEVICE_NOT_READY;
    } else {
        switch (request->operation) {
        case OTA_STORAGE_BEGIN:
            request->status = ota_staging_begin(
                &context->ota_staging, request->length);
            break;
        case OTA_STORAGE_ERASE_NEXT:
            request->status = ota_staging_erase_next(
                &context->ota_staging, &request->complete);
            break;
        case OTA_STORAGE_WRITE:
            request->status = ota_staging_write(
                &context->ota_staging, request->offset,
                request->data, request->length);
            break;
        case OTA_STORAGE_COMMIT_METADATA:
            request->status = ota_staging_commit_metadata(
                &context->ota_staging, request->data, request->length);
            break;
        default:
            request->status = ERR_UNSUPPORTED;
            break;
        }
        if (needs_flash_lock != 0u) {
            (void)power_lock_release(context, PM_LOCK_FLASH_WRITE);
        }
    }
    notify_ota_waiter(request->waiter);
}

static void storage_task(void *argument)
{
    app_context_t *context = argument;
    uint8_t fault_archive_complete = 0u;
    uint8_t power_suspended = 0u;
    uint32_t last_activity_ms =
        (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    for (;;) {
        uint32_t log_ready = 0u;
        uint32_t alarm_ready = 0u;
        uint32_t config_ready = 0u;
        uint32_t ota_ready = 0u;
        uint8_t flash_lock_held = 0u;
        uint8_t had_activity = 0u;
        QueueSetMemberHandle_t ready;
        uint32_t now_ms =
            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        EventBits_t power_bits = xEventGroupGetBits(channels.system_events);
        power_mode_t current_power_mode;

        app_critical_enter(context);
        current_power_mode = context->power.current_mode;
        app_critical_exit(context);

        if ((power_bits & SYSTEM_EVENT_POWER_QUIESCE_REQUEST) != 0u) {
            if (power_suspended == 0u) {
                status_t status = SYS_OK;

                if (context->storage.health.mounted != 0u &&
                    power_lock_acquire(context, PM_LOCK_FLASH_WRITE) ==
                        SYS_OK) {
                    status = storage_subsystem_power_down(
                        &context->storage);
                    (void)power_lock_release(context,
                                             PM_LOCK_FLASH_WRITE);
                } else if (context->storage.health.mounted != 0u) {
                    status = ERR_DEVICE_NOT_READY;
                }
                if (status == SYS_OK) {
                    power_suspended = 1u;
                    xEventGroupSetBits(channels.system_events,
                        SYSTEM_EVENT_POWER_ACK_STORAGE);
                } else {
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_FAULT_ACTIVE);
                }
            }
            app_context_mark_alive(context, GATEWAY_TASK_STORAGE);
            vTaskDelay(pdMS_TO_TICKS(20u));
            continue;
        }
        if (power_suspended != 0u) {
            status_t status = SYS_OK;

            if (context->storage.health.mounted != 0u &&
                power_lock_acquire(context, PM_LOCK_FLASH_WRITE) == SYS_OK) {
                status = storage_subsystem_wake(&context->storage);
                (void)power_lock_release(context, PM_LOCK_FLASH_WRITE);
            } else if (context->storage.health.mounted != 0u) {
                status = ERR_DEVICE_NOT_READY;
            }
            xEventGroupClearBits(channels.system_events,
                                 SYSTEM_EVENT_POWER_ACK_STORAGE);
            power_suspended = 0u;
            last_activity_ms = now_ms;
            if (status != SYS_OK) {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
        }

        if (context->storage.health.mounted == 0u) {
            if (power_lock_acquire(context,
                                   PM_LOCK_FLASH_WRITE) == SYS_OK) {
                context->storage_startup_status =
                    storage_subsystem_start(&context->storage);
                (void)power_lock_release(context, PM_LOCK_FLASH_WRITE);
            } else {
                context->storage_startup_status = ERR_DEVICE_NOT_READY;
            }
            if (context->storage_startup_status == SYS_OK) {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_STORAGE_READY);
            } else {
                xEventGroupClearBits(channels.system_events,
                                     SYSTEM_EVENT_STORAGE_READY);
                app_context_mark_alive(context, GATEWAY_TASK_STORAGE);
                vTaskDelay(pdMS_TO_TICKS(1000u));
                continue;
            }
        }

        if (fault_archive_complete == 0u &&
            (context->initialization_mask & APP_INITIALIZED_RELIABILITY) !=
                0u) {
            fault_record_t record;
            status_t fault_status = fault_recorder_get(
                &context->fault_recorder, &record);

            if (fault_status == ERR_DEVICE_NOT_READY) {
                fault_archive_complete = 1u;
            } else if (fault_status == SYS_OK &&
                       power_lock_acquire(context, PM_LOCK_FLASH_WRITE) ==
                           SYS_OK) {
                if (xSemaphoreTake(channels.config_mutex,
                                   portMAX_DELAY) == pdPASS) {
                    fault_status = storage_subsystem_archive_fault(
                        &context->storage, &record);
                    xSemaphoreGive(channels.config_mutex);
                } else {
                    fault_status = ERR_TIMEOUT;
                }
                (void)power_lock_release(context, PM_LOCK_FLASH_WRITE);
                if (fault_status == SYS_OK) {
                    fault_archive_complete = 1u;
                } else {
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_FAULT_ACTIVE);
                }
            }
        }

        ready = xQueueSelectFromSet(channels.storage_set,
                                    pdMS_TO_TICKS(1000U));
        had_activity = ready != 0 ? 1u : 0u;
        if (had_activity != 0u &&
            context->storage.health.powered_down != 0u) {
            status_t wake_status = ERR_DEVICE_NOT_READY;

            if (power_lock_acquire(context, PM_LOCK_FLASH_WRITE) == SYS_OK) {
                wake_status = storage_subsystem_wake(&context->storage);
                (void)power_lock_release(context, PM_LOCK_FLASH_WRITE);
            }
            if (wake_status != SYS_OK) {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
                app_context_mark_alive(context, GATEWAY_TASK_STORAGE);
                continue;
            }
        }
        while (ready != 0) {
            if (ready == channels.storage_alarm) {
                alarm_ready++;
            } else if (ready == channels.storage_config) {
                config_ready++;
            } else if (ready == channels.storage_log) {
                log_ready++;
            } else if (ready == channels.ota_storage_request) {
                ota_ready++;
            }
            ready = xQueueSelectFromSet(channels.storage_set, 0u);
        }

        while (ota_ready != 0u) {
            ota_storage_request_t *request = 0;

            ota_ready--;
            if (xQueueReceive(channels.ota_storage_request, &request,
                              0u) == pdPASS) {
                process_ota_storage_request(context, request);
                app_context_mark_alive(context, GATEWAY_TASK_STORAGE);
            }
        }

        if ((alarm_ready != 0u || config_ready != 0u || log_ready != 0u) &&
            power_lock_acquire(context, PM_LOCK_FLASH_WRITE) != SYS_OK) {
            xEventGroupSetBits(channels.system_events,
                               SYSTEM_EVENT_FAULT_ACTIVE);
            app_context_mark_alive(context, GATEWAY_TASK_STORAGE);
            continue;
        }
        if (alarm_ready != 0u || config_ready != 0u || log_ready != 0u) {
            flash_lock_held = 1u;
        }

        while (alarm_ready != 0u) {
            gateway_storage_alarm_request_t request;

            alarm_ready--;
            if (xQueueReceive(channels.storage_alarm, &request, 0u) ==
                    pdPASS &&
                storage_subsystem_append_alarm(&context->storage,
                                               &request) != SYS_OK) {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
        }
        while (config_ready != 0u) {
            gateway_storage_config_request_t request;
            status_t status = ERR_IO;

            config_ready--;
            if (xQueueReceive(channels.storage_config, &request, 0u) !=
                pdPASS) {
                continue;
            }
            if (xSemaphoreTake(channels.config_mutex, portMAX_DELAY) ==
                pdPASS) {
                if (alarm_subsystem_active_count(&context->alarm) != 0u) {
                    status = ERR_DEVICE_NOT_READY;
                } else {
                    gateway_runtime_config_t previous;

                    status = config_subsystem_get(&context->config,
                                                  &previous);
                    if (status == SYS_OK) {
                        status = alarm_subsystem_reconfigure(
                            &context->alarm, &request.config);
                    }
                    if (status == SYS_OK) {
                        status = storage_subsystem_save_config(
                            &context->storage, &request);
                        if (status != SYS_OK) {
                            (void)alarm_subsystem_reconfigure(
                                &context->alarm, &previous);
                        }
                    }
                    if (status == SYS_OK) {
                        status = config_subsystem_commit(&context->config,
                                                         &request);
                    }
                }
                if (status != SYS_OK) {
                    (void)config_subsystem_reject(&context->config,
                                                  request.request_id,
                                                  status);
                }
                xSemaphoreGive(channels.config_mutex);
            }
            if (status != SYS_OK) {
                context->storage_config_publish_drops++;
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
        }
        while (log_ready != 0u) {
            gateway_storage_log_request_t request;

            log_ready--;
            if (xQueueReceive(channels.storage_log, &request, 0u) ==
                    pdPASS &&
                storage_subsystem_append_log(&context->storage,
                                             &request) != SYS_OK) {
                context->storage_log_publish_drops++;
            }
        }
        if (flash_lock_held != 0u) {
            (void)power_lock_release(context, PM_LOCK_FLASH_WRITE);
        }
        if (had_activity != 0u) {
            last_activity_ms = (uint32_t)(xTaskGetTickCount() *
                                          portTICK_PERIOD_MS);
        } else if (current_power_mode == POWER_ECO &&
                   context->storage.health.mounted != 0u &&
                   context->storage.health.powered_down == 0u &&
                   now_ms - last_activity_ms >= STORAGE_ECO_IDLE_MS &&
                   (power_bits & SYSTEM_EVENT_OTA_ACTIVE) == 0u &&
                   power_lock_acquire(context, PM_LOCK_FLASH_WRITE) ==
                       SYS_OK) {
            status_t power_status = storage_subsystem_power_down(
                &context->storage);

            (void)power_lock_release(context, PM_LOCK_FLASH_WRITE);
            if (power_status != SYS_OK) {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
        }
        app_context_mark_alive(context, GATEWAY_TASK_STORAGE);
    }
}

static void ui_task(void *argument)
{
    app_context_t *context = argument;
    gateway_system_snapshot_t snapshot = context->snapshot;
    gateway_runtime_config_t runtime_config = context->config.active;
    status_t startup_status;
    uint8_t ui_lock_held = 0u;
    uint8_t power_suspended = 0u;
    uint32_t ui_lock_release_ms = 0u;
    ui_power_state_t applied_power_state = UI_POWER_ACTIVE;

    (void)ui_subsystem_set_action_handler(&context->ui,
                                          handle_ui_action, context);
    startup_status = ui_subsystem_start(&context->ui);
    context->ui_startup_status = startup_status;
    if (startup_status == SYS_OK) {
        xEventGroupSetBits(channels.system_events, SYSTEM_EVENT_UI_READY);
    } else {
        xEventGroupClearBits(channels.system_events, SYSTEM_EVENT_UI_READY);
    }

    for (;;) {
        ui_page_id_t page;
        uint32_t notification_value;
        EventBits_t power_bits = xEventGroupGetBits(channels.system_events);

        if ((power_bits & SYSTEM_EVENT_POWER_QUIESCE_REQUEST) != 0u) {
            if (power_suspended == 0u) {
                status_t status = startup_status == SYS_OK
                    ? ui_subsystem_set_power_state(
                          &context->ui, UI_POWER_SUSPENDED)
                    : SYS_OK;

                if (status == SYS_OK) {
                    if (ui_lock_held != 0u) {
                        (void)power_lock_release(context,
                                                PM_LOCK_UI_ACTIVE);
                        ui_lock_held = 0u;
                    }
                    applied_power_state = UI_POWER_SUSPENDED;
                    power_suspended = 1u;
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_POWER_ACK_UI);
                } else {
                    xEventGroupSetBits(channels.system_events,
                                       SYSTEM_EVENT_FAULT_ACTIVE);
                }
            }
            app_context_mark_alive(context, GATEWAY_TASK_UI);
            vTaskDelay(pdMS_TO_TICKS(20u));
            continue;
        }
        if (power_suspended != 0u) {
            status_t status = startup_status == SYS_OK
                ? ui_subsystem_set_power_state(&context->ui,
                      context->power.current_mode == POWER_ECO
                          ? UI_POWER_ECO : UI_POWER_ACTIVE)
                : SYS_OK;

            if (status == SYS_OK) {
                applied_power_state = context->power.current_mode == POWER_ECO
                    ? UI_POWER_ECO : UI_POWER_ACTIVE;
            } else {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
            xEventGroupClearBits(channels.system_events,
                                 SYSTEM_EVENT_POWER_ACK_UI);
            power_suspended = 0u;
        }

        if (startup_status == SYS_OK) {
            const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() *
                                               portTICK_PERIOD_MS);
            power_mode_t power_mode;
            ui_power_state_t requested_power_state;

            (void)xQueueReceive(channels.ui_snapshot, &snapshot, 0U);
            while (xQueueReceive(channels.ui_command, &page, 0u) == pdPASS) {
                (void)ui_subsystem_navigate(&context->ui, page);
            }
            if (xSemaphoreTake(channels.config_mutex,
                               pdMS_TO_TICKS(5u)) == pdPASS) {
                (void)config_subsystem_get(&context->config,
                                           &runtime_config);
                xSemaphoreGive(channels.config_mutex);
                (void)ui_subsystem_process(
                    &context->ui, now_ms,
                    &snapshot, &runtime_config);
            }
            if (ui_subsystem_take_input_activity(&context->ui) != 0u) {
                app_critical_enter(context);
                power_manager_note_activity(&context->power, now_ms);
                app_critical_exit(context);
                if (ui_lock_held == 0u &&
                    power_lock_acquire(context, PM_LOCK_UI_ACTIVE) == SYS_OK) {
                    ui_lock_held = 1u;
                }
                ui_lock_release_ms = now_ms + UI_ACTIVE_LOCK_HOLD_MS;
            }
            if (ui_lock_held != 0u &&
                (int32_t)(now_ms - ui_lock_release_ms) >= 0 &&
                power_lock_release(context, PM_LOCK_UI_ACTIVE) == SYS_OK) {
                ui_lock_held = 0u;
            }
            app_critical_enter(context);
            power_mode = context->power.current_mode;
            app_critical_exit(context);
            requested_power_state = ui_lock_held != 0u ||
                                    power_mode == POWER_ACTIVE
                ? UI_POWER_ACTIVE
                : (power_mode == POWER_ECO
                    ? UI_POWER_ECO : UI_POWER_SUSPENDED);
            if (requested_power_state != applied_power_state &&
                ui_subsystem_set_power_state(
                    &context->ui, requested_power_state) == SYS_OK) {
                applied_power_state = requested_power_state;
            }
        }
        app_context_mark_alive(context, GATEWAY_TASK_UI);
        (void)xTaskNotifyWait(0u, UINT32_MAX, &notification_value,
                             pdMS_TO_TICKS(startup_status == SYS_OK
                                 ? 10u : 1000u));
    }
}

static void cli_task(void *argument)
{
    app_context_t *context = argument;
    status_t startup_status;

    (void)cli_subsystem_set_command_handler(&context->cli,
                                            handle_cli_command, context);
    startup_status = cli_subsystem_start(&context->cli);
    context->cli_startup_status = startup_status;
    if (startup_status == SYS_OK) {
        xEventGroupSetBits(channels.system_events, SYSTEM_EVENT_CLI_READY);
    } else {
        xEventGroupClearBits(channels.system_events, SYSTEM_EVENT_CLI_READY);
    }

    for (;;) {
        if (startup_status == SYS_OK) {
            (void)cli_subsystem_process(&context->cli, 1000u);
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000u));
        }
        app_context_mark_alive(context, GATEWAY_TASK_CLI);
    }
}

static status_t submit_config_patch(app_context_t *context,
                                    const config_patch_t *patch,
                                    uint32_t *request_id)
{
    gateway_storage_config_request_t request;
    status_t status;

    if (xSemaphoreTake(channels.config_mutex, pdMS_TO_TICKS(50u)) !=
        pdPASS) {
        return ERR_TIMEOUT;
    }
    if (alarm_subsystem_active_count(&context->alarm) != 0u) {
        status = ERR_DEVICE_NOT_READY;
    } else {
        status = config_subsystem_prepare(&context->config, patch,
                                          &request);
    }
    xSemaphoreGive(channels.config_mutex);
    if (status != SYS_OK) {
        return status;
    }
    status = app_rtos_submit_config(&request);
    if (status != SYS_OK) {
        if (xSemaphoreTake(channels.config_mutex,
                           pdMS_TO_TICKS(50u)) == pdPASS) {
            (void)config_subsystem_reject(&context->config,
                                          request.request_id, status);
            xSemaphoreGive(channels.config_mutex);
        }
        return status;
    }
    if (request_id != 0) {
        *request_id = request.request_id;
    }
    return SYS_OK;
}

static status_t handle_ui_action(void *opaque, const ui_action_t *action)
{
    app_context_t *context = opaque;

    if (context == 0 || action == 0) {
        return ERR_INVALID_ARG;
    }
    if (action->type == UI_ACTION_CONFIG_PATCH) {
        return submit_config_patch(context,
                                   &action->payload.config_patch, 0);
    }
    if (action->type == UI_ACTION_OTA_START ||
        action->type == UI_ACTION_OTA_CANCEL) {
        gateway_ota_command_t command;

        command.type = action->type == UI_ACTION_OTA_START
            ? GATEWAY_OTA_COMMAND_START : GATEWAY_OTA_COMMAND_CANCEL;
        command.request_id = context->heartbeat[GATEWAY_TASK_UI];
        return app_rtos_submit_ota_command(&command);
    }
    return ERR_UNSUPPORTED;
}

static status_t acknowledge_alarm(app_context_t *context, uint32_t event_id)
{
    gateway_alarm_event_t event;
    gateway_storage_alarm_request_t request;
    status_t status;

    if (xSemaphoreTake(channels.config_mutex, pdMS_TO_TICKS(50u)) !=
        pdPASS) {
        return ERR_TIMEOUT;
    }
    status = alarm_subsystem_acknowledge(&context->alarm, event_id, &event);
    xSemaphoreGive(channels.config_mutex);
    if (status != SYS_OK) {
        return status;
    }
    request.event = event;
    return xQueueSend(channels.storage_alarm, &request,
                      pdMS_TO_TICKS(20u)) == pdPASS
        ? SYS_OK : ERR_QUEUE_FULL;
}

static status_t handle_cli_command(void *opaque,
                                   const cli_command_t *command,
                                   char *response, size_t capacity)
{
    app_context_t *context = opaque;
    EventBits_t bits;
    status_t status = SYS_OK;

    if (context == 0 || command == 0 || response == 0 || capacity == 0u) {
        return ERR_INVALID_ARG;
    }
    bits = xEventGroupGetBits(channels.system_events);
    switch (command->id) {
    case CLI_COMMAND_HELP:
        (void)snprintf(response, capacity,
            "help | status | rtos [task|queue|runtime|timing] | sensor list\r\n"
            "alarm list|ack ID | fault show|clear | slot status\r\n"
            "mqtt status | storage status | config show\r\n"
            "config set POINT FIELD VALUE | ui page NAME\r\n"
            "ota status|check|start|apply|cancel | power status|stats|lock\r\n"
            "power stop MS CONFIRM | power standby CONFIRM | power cancel\r\n"
            "debug only: fault inject hardfault|watchdog CONFIRM");
        break;
    case CLI_COMMAND_STATUS:
        if (xSemaphoreTake(channels.snapshot_mutex,
                           pdMS_TO_TICKS(20u)) == pdPASS) {
            gateway_system_snapshot_t snapshot = context->snapshot;
            xSemaphoreGive(channels.snapshot_mutex);
            (void)snprintf(response, capacity,
                "seq=%lu point=%u value=%ld quality=%u alarms=%lu "
                "relay=%u flags=0x%08lX",
                (unsigned long)snapshot.sequence,
                (unsigned int)snapshot.latest.point_id,
                (long)snapshot.latest.engineering_value,
                (unsigned int)snapshot.latest.quality,
                (unsigned long)snapshot.active_alarm_count,
                (unsigned int)snapshot.relay_energized,
                (unsigned long)bits);
        } else {
            status = ERR_TIMEOUT;
        }
        break;
    case CLI_COMMAND_RTOS:
        {
            supervisor_health_t health;

            (void)supervisor_subsystem_get_health(&context->supervisor,
                                                  &health);
            (void)snprintf(response, capacity,
            "healthy=%u stale=0x%08lX feeds=%lu faults=%lu "
            "heartbeat sup=%lu acq=%lu hub=%lu modbus=%lu can=%lu "
            "net=%lu ota=%lu store=%lu ui=%lu cli=%lu",
            (unsigned int)health.healthy,
            (unsigned long)health.stale_task_mask,
            (unsigned long)health.watchdog_refreshes,
            (unsigned long)health.latched_faults,
            (unsigned long)context->heartbeat[GATEWAY_TASK_SUPERVISOR],
            (unsigned long)context->heartbeat[GATEWAY_TASK_ACQUISITION],
            (unsigned long)context->heartbeat[GATEWAY_TASK_DATA_HUB],
            (unsigned long)context->heartbeat[GATEWAY_TASK_MODBUS],
            (unsigned long)context->heartbeat[GATEWAY_TASK_CAN],
            (unsigned long)context->heartbeat[GATEWAY_TASK_NETWORK],
            (unsigned long)context->heartbeat[GATEWAY_TASK_OTA],
            (unsigned long)context->heartbeat[GATEWAY_TASK_STORAGE],
            (unsigned long)context->heartbeat[GATEWAY_TASK_UI],
            (unsigned long)context->heartbeat[GATEWAY_TASK_CLI]);
        }
        break;
    case CLI_COMMAND_RTOS_TASK:
        (void)snprintf(response, capacity,
            "stack_hwm words sup=%lu acq=%lu hub=%lu modbus=%lu can=%lu "
            "net=%lu ota=%lu store=%lu ui=%lu cli=%lu samples=%lu",
            (unsigned long)context->rtos_diagnostics.stack_high_water[0],
            (unsigned long)context->rtos_diagnostics.stack_high_water[1],
            (unsigned long)context->rtos_diagnostics.stack_high_water[2],
            (unsigned long)context->rtos_diagnostics.stack_high_water[3],
            (unsigned long)context->rtos_diagnostics.stack_high_water[4],
            (unsigned long)context->rtos_diagnostics.stack_high_water[5],
            (unsigned long)context->rtos_diagnostics.stack_high_water[6],
            (unsigned long)context->rtos_diagnostics.stack_high_water[7],
            (unsigned long)context->rtos_diagnostics.stack_high_water[8],
            (unsigned long)context->rtos_diagnostics.stack_high_water[9],
            (unsigned long)context->rtos_diagnostics.samples);
        break;
    case CLI_COMMAND_RTOS_QUEUE:
        (void)snprintf(response, capacity,
            "queue cur/high measure=%u/%u can=%u/%u ui=%u/%u "
            "tele=%u/%u alarm=%u/%u netctl=%u/%u result=%u/%u "
            "ota=%u/%u log=%u/%u salarm=%u/%u config=%u/%u uicmd=%u/%u "
            "otanet=%u/%u otastore=%u/%u power=%u/%u",
            context->rtos_diagnostics.queue_current[0],
            context->rtos_diagnostics.queue_high_water[0],
            context->rtos_diagnostics.queue_current[1],
            context->rtos_diagnostics.queue_high_water[1],
            context->rtos_diagnostics.queue_current[2],
            context->rtos_diagnostics.queue_high_water[2],
            context->rtos_diagnostics.queue_current[3],
            context->rtos_diagnostics.queue_high_water[3],
            context->rtos_diagnostics.queue_current[4],
            context->rtos_diagnostics.queue_high_water[4],
            context->rtos_diagnostics.queue_current[5],
            context->rtos_diagnostics.queue_high_water[5],
            context->rtos_diagnostics.queue_current[6],
            context->rtos_diagnostics.queue_high_water[6],
            context->rtos_diagnostics.queue_current[7],
            context->rtos_diagnostics.queue_high_water[7],
            context->rtos_diagnostics.queue_current[8],
            context->rtos_diagnostics.queue_high_water[8],
            context->rtos_diagnostics.queue_current[9],
            context->rtos_diagnostics.queue_high_water[9],
            context->rtos_diagnostics.queue_current[10],
            context->rtos_diagnostics.queue_high_water[10],
            context->rtos_diagnostics.queue_current[11],
            context->rtos_diagnostics.queue_high_water[11],
            context->rtos_diagnostics.queue_current[12],
            context->rtos_diagnostics.queue_high_water[12],
            context->rtos_diagnostics.queue_current[13],
            context->rtos_diagnostics.queue_high_water[13],
            context->rtos_diagnostics.queue_current[14],
            context->rtos_diagnostics.queue_high_water[14]);
        break;
    case CLI_COMMAND_RTOS_RUNTIME:
        (void)snprintf(response, capacity,
            "cpu permille sup=%u acq=%u hub=%u modbus=%u can=%u net=%u "
            "ota=%u store=%u ui=%u cli=%u idle=%u system=%u samples=%lu "
            "errors=%lu",
            context->rtos_diagnostics.cpu_permille[0],
            context->rtos_diagnostics.cpu_permille[1],
            context->rtos_diagnostics.cpu_permille[2],
            context->rtos_diagnostics.cpu_permille[3],
            context->rtos_diagnostics.cpu_permille[4],
            context->rtos_diagnostics.cpu_permille[5],
            context->rtos_diagnostics.cpu_permille[6],
            context->rtos_diagnostics.cpu_permille[7],
            context->rtos_diagnostics.cpu_permille[8],
            context->rtos_diagnostics.cpu_permille[9],
            context->rtos_diagnostics.idle_cpu_permille,
            context->rtos_diagnostics.system_cpu_permille,
            (unsigned long)context->rtos_diagnostics.runtime_samples,
            (unsigned long)context->rtos_diagnostics.runtime_errors);
        break;
    case CLI_COMMAND_RTOS_TIMING:
        {
            periodic_timing_stats_t timing;

            app_critical_enter(context);
            status = periodic_timing_monitor_get(
                &context->acquisition_timing, &timing);
            app_critical_exit(context);
            if (status == SYS_OK) {
                (void)snprintf(response, capacity,
                    "acq period=%lu tolerance=%lu releases=%lu "
                    "intervals=%lu last=%lu jitter=%ld release_late=%ld "
                    "min=%lu max=%lu early_max=%lu late_max=%lu misses=%lu",
                    (unsigned long)timing.expected_period_ms,
                    (unsigned long)timing.release_tolerance_ms,
                    (unsigned long)timing.releases,
                    (unsigned long)timing.intervals,
                    (unsigned long)timing.last_interval_ms,
                    (long)timing.last_jitter_ms,
                    (long)timing.last_release_lateness_ms,
                    (unsigned long)timing.min_interval_ms,
                    (unsigned long)timing.max_interval_ms,
                    (unsigned long)timing.max_early_ms,
                    (unsigned long)timing.max_late_ms,
                    (unsigned long)timing.deadline_misses);
            } else {
                (void)snprintf(response, capacity,
                               "acq timing: %s", error_to_string(status));
            }
        }
        break;
    case CLI_COMMAND_SENSOR_LIST:
        (void)snprintf(response, capacity,
            "1001 ADS1115 4-20mA | 1002 MAX31865 PT100 | "
            "1003/1004 SHT30 | 2001 Modbus | 3001 CAN");
        break;
    case CLI_COMMAND_ALARM_LIST:
        if (xSemaphoreTake(channels.snapshot_mutex,
                           pdMS_TO_TICKS(20u)) == pdPASS) {
            uint32_t active = context->snapshot.active_alarm_count;
            xSemaphoreGive(channels.snapshot_mutex);
            (void)snprintf(response, capacity, "active=%lu",
                (unsigned long)active);
        } else {
            status = ERR_TIMEOUT;
        }
        break;
    case CLI_COMMAND_ALARM_ACK:
        status = acknowledge_alarm(context,
                                   command->argument.alarm_event_id);
        (void)snprintf(response, capacity, "alarm ack %lu: %s",
            (unsigned long)command->argument.alarm_event_id,
            error_to_string(status));
        break;
    case CLI_COMMAND_MQTT_STATUS:
        (void)snprintf(response, capacity, "network=%s mqtt=%s ota_lease=%s",
            (bits & SYSTEM_EVENT_NETWORK_UP) != 0u ? "up" : "down",
            (bits & SYSTEM_EVENT_MQTT_READY) != 0u ? "ready" : "not-ready",
            (bits & SYSTEM_EVENT_OTA_ACTIVE) != 0u ? "active" : "idle");
        break;
    case CLI_COMMAND_STORAGE_STATUS:
        {
            storage_health_t health;

            memset(&health, 0, sizeof(health));
            app_critical_enter(context);
            (void)storage_subsystem_get_health(&context->storage, &health);
            app_critical_exit(context);
            (void)snprintf(response, capacity,
                "storage=%s q_log=%lu q_alarm=%lu q_config=%lu "
                "crash_valid=%u crash_seq=%lu archives=%lu duplicates=%lu "
                "invalid=%lu asleep=%u down=%lu wake=%lu power_fail=%lu "
                "last=%s",
                (bits & SYSTEM_EVENT_STORAGE_READY) != 0u ? "ready" : "down",
                (unsigned long)uxQueueMessagesWaiting(channels.storage_log),
                (unsigned long)uxQueueMessagesWaiting(channels.storage_alarm),
                (unsigned long)uxQueueMessagesWaiting(channels.storage_config),
                (unsigned int)health.latest_crash_valid,
                (unsigned long)health.latest_crash_sequence,
                (unsigned long)health.crash_archives,
                (unsigned long)health.crash_duplicates,
                (unsigned long)health.mount_invalid_records,
                (unsigned int)health.powered_down,
                (unsigned long)health.power_downs,
                (unsigned long)health.wakeups,
                (unsigned long)health.power_failures,
                error_to_string(health.last_error));
        }
        break;
    case CLI_COMMAND_CONFIG_SHOW:
        if (xSemaphoreTake(channels.config_mutex,
                           pdMS_TO_TICKS(50u)) == pdPASS) {
            gateway_runtime_config_t config;
            config_health_t health;
            (void)config_subsystem_get(&context->config, &config);
            (void)config_subsystem_get_health(&context->config, &health);
            xSemaphoreGive(channels.config_mutex);
            (void)snprintf(response, capacity,
                "revision=%lu rules=%u pending=%lu last_request=%lu "
                "last=%s first_point=%u high=%ld low=%ld hysteresis=%ld",
                (unsigned long)config.revision,
                (unsigned int)config.rule_count,
                (unsigned long)health.pending_requests,
                (unsigned long)health.last_request_id,
                error_to_string(health.last_status),
                (unsigned int)config.rules[0].point_id,
                (long)config.rules[0].high_threshold,
                (long)config.rules[0].low_threshold,
                (long)config.rules[0].hysteresis);
        } else {
            status = ERR_TIMEOUT;
        }
        break;
    case CLI_COMMAND_CONFIG_SET:
        {
            uint32_t request_id = 0u;
            status = submit_config_patch(
                context, &command->argument.config_patch, &request_id);
            (void)snprintf(response, capacity,
                "config request=%lu status=%s",
                (unsigned long)request_id, error_to_string(status));
        }
        break;
    case CLI_COMMAND_UI_PAGE:
        status = app_rtos_submit_ui_page(command->argument.ui_page);
        (void)snprintf(response, capacity, "ui page=%s status=%s",
            ui_page_name(command->argument.ui_page),
            error_to_string(status));
        break;
    case CLI_COMMAND_OTA_STATUS:
        if (context->ota.initialized != 0u) {
            ota_status_t ota_status;

            if (ota_manager_get_status(&context->ota, &ota_status) ==
                SYS_OK) {
                (void)snprintf(response, capacity,
                    "ota=%s lease=%s bytes=%lu/%lu chunks=%lu target=%u "
                    "crc=%u sha=%u last=%s",
                    ota_state_name(ota_status.state),
                    (bits & SYSTEM_EVENT_OTA_ACTIVE) != 0u
                        ? "active" : "idle",
                    (unsigned long)ota_status.bytes_downloaded,
                    (unsigned long)ota_status.total_bytes,
                    (unsigned long)ota_status.chunk_count,
                    (unsigned int)ota_status.target_slot,
                    (unsigned int)ota_status.crc_verified,
                    (unsigned int)ota_status.sha256_verified,
                    error_to_string(ota_status.last_error));
            } else {
                (void)snprintf(response, capacity, "ota=not-ready");
            }
        } else {
            (void)snprintf(response, capacity, "ota=idle lease=%s",
                (bits & SYSTEM_EVENT_OTA_ACTIVE) != 0u
                    ? "active" : "idle");
        }
        break;
    case CLI_COMMAND_OTA_CHECK:
    case CLI_COMMAND_OTA_START:
    case CLI_COMMAND_OTA_APPLY:
    case CLI_COMMAND_OTA_CANCEL:
        {
            gateway_ota_command_t ota;
            if (command->id == CLI_COMMAND_OTA_CHECK) {
                ota.type = GATEWAY_OTA_COMMAND_CHECK;
            } else if (command->id == CLI_COMMAND_OTA_START) {
                ota.type = GATEWAY_OTA_COMMAND_START;
            } else if (command->id == CLI_COMMAND_OTA_APPLY) {
                ota.type = GATEWAY_OTA_COMMAND_APPLY;
            } else {
                ota.type = GATEWAY_OTA_COMMAND_CANCEL;
            }
            ota.request_id = context->heartbeat[GATEWAY_TASK_CLI];
            status = app_rtos_submit_ota_command(&ota);
            (void)snprintf(response, capacity, "ota request=%lu status=%s",
                (unsigned long)ota.request_id, error_to_string(status));
        }
        break;
    case CLI_COMMAND_POWER_STATUS:
        {
        deep_power_health_t deep_health;

        app_critical_enter(context);
        (void)deep_power_controller_get_health(&context->deep_power,
                                               &deep_health);
        app_critical_exit(context);
        (void)snprintf(response, capacity,
            "mode=%s policy=%s deepest=%s locks=0x%02lX leak=0x%02lX "
            "iwdg_remaining_ms=%lu deep_state=%u pending=%u "
            "stop_cap=%u standby_cap=%u ack=0x%02lX deep_last=%s last=%s",
            power_mode_name(context->power.current_mode),
            power_policy_name(context->power.policy),
            power_mode_name(power_manager_deepest_allowed(&context->power)),
            (unsigned long)context->power.stats.lock_mask,
            (unsigned long)context->power.stats.leak_mask,
            (unsigned long)watchdog_device_remaining_ms(&context->watchdog),
            (unsigned int)deep_health.state,
            (unsigned int)deep_health.request_pending,
            (unsigned int)context->deep_power.config.stop_enabled,
            (unsigned int)context->deep_power.config.standby_enabled,
            (unsigned long)deep_power_quiesced_mask(bits),
            error_to_string(deep_health.last_error),
            error_to_string(context->power.last_error));
        }
        break;
    case CLI_COMMAND_POWER_STATS:
        (void)snprintf(response, capacity,
            "acquire=%lu release=%lu release_err=%lu leak=%lu "
            "sleep attempt=%lu enter=%lu reject_short=%lu "
            "reject_lock=%lu reject_iwdg=%lu planned_ms=%lu max_plan_ms=%lu "
            "deep_req=%lu wait=%lu stop=%lu standby=%lu restore=%lu fail=%lu "
            "critical_max_cycles=%lu nesting_max=%u pair_err=%lu",
            (unsigned long)context->power.stats.acquire_count,
            (unsigned long)context->power.stats.release_count,
            (unsigned long)context->power.stats.release_errors,
            (unsigned long)context->power.stats.leak_events,
            (unsigned long)context->power.stats.sleep_attempts,
            (unsigned long)context->power.stats.sleep_entries,
            (unsigned long)context->power.stats.sleep_rejections[0],
            (unsigned long)context->power.stats.sleep_rejections[1],
            (unsigned long)context->power.stats.sleep_rejections[2],
            (unsigned long)context->power.stats.cumulative_sleep_budget_ms,
            (unsigned long)context->power.stats.longest_sleep_budget_ms,
            (unsigned long)context->deep_power.health.request_count,
            (unsigned long)context->deep_power.health.quiesce_waits,
            (unsigned long)context->deep_power.health.stop_entries,
            (unsigned long)context->deep_power.health.standby_entries,
            (unsigned long)context->deep_power.health.restore_count,
            (unsigned long)context->deep_power.health.failures,
            (unsigned long)context->critical_timing.longest_cycles,
            (unsigned int)context->critical_timing.maximum_nesting,
            (unsigned long)context->critical_timing.pairing_errors);
        break;
    case CLI_COMMAND_POWER_LOCK:
        (void)snprintf(response, capacity,
            "mask=0x%02lX ota=%u flash=%u net=%u modbus=%u can=%u "
            "ui=%u alarm=%u",
            (unsigned long)context->power.stats.lock_mask,
            context->power.lock_refcount[PM_LOCK_OTA],
            context->power.lock_refcount[PM_LOCK_FLASH_WRITE],
            context->power.lock_refcount[PM_LOCK_NETWORK_TX],
            context->power.lock_refcount[PM_LOCK_MODBUS_TRANSACTION],
            context->power.lock_refcount[PM_LOCK_CAN_MONITORING],
            context->power.lock_refcount[PM_LOCK_UI_ACTIVE],
            context->power.lock_refcount[PM_LOCK_ALARM_ACTIVE]);
        break;
    case CLI_COMMAND_POWER_STOP:
        {
        power_command_t request = {
            POWER_COMMAND_REQUEST_STOP,
            command->argument.power_duration_ms
        };

        if (context->deep_power.config.stop_enabled == 0u) {
            status = ERR_UNSUPPORTED;
        } else if (request.duration_ms <
                       context->deep_power.config.minimum_stop_ms ||
                   request.duration_ms >
                       context->deep_power.config.maximum_stop_ms) {
            status = ERR_INVALID_ARG;
        } else {
            status = xQueueSend(channels.power_command, &request, 0u) ==
                         pdPASS ? SYS_OK : ERR_QUEUE_FULL;
        }
        (void)snprintf(response, capacity,
            "power stop queued_ms=%lu status=%s",
            (unsigned long)command->argument.power_duration_ms,
            error_to_string(status));
        }
        break;
    case CLI_COMMAND_POWER_STANDBY:
        {
        power_command_t request = {
            POWER_COMMAND_REQUEST_STANDBY,
            0u
        };

        status = context->deep_power.config.standby_enabled == 0u
            ? ERR_UNSUPPORTED
            : (xQueueSend(channels.power_command, &request, 0u) == pdPASS
                ? SYS_OK : ERR_QUEUE_FULL);
        (void)snprintf(response, capacity,
            "power standby queued status=%s", error_to_string(status));
        }
        break;
    case CLI_COMMAND_POWER_CANCEL:
        {
        power_command_t request = { POWER_COMMAND_CANCEL, 0u };

        status = xQueueSend(channels.power_command, &request, 0u) == pdPASS
            ? SYS_OK : ERR_QUEUE_FULL;
        (void)snprintf(response, capacity,
            "power cancel queued status=%s", error_to_string(status));
        }
        break;
    case CLI_COMMAND_SLOT_STATUS:
        (void)snprintf(response, capacity,
            "running=%u active=%u pending=%u state=%u confirmed=%u "
            "attempts=%lu commits=%lu seq=%lu last=%s",
            (unsigned int)context->boot_confirmation.health.running_slot,
            (unsigned int)context->boot_confirmation.health.active_slot,
            (unsigned int)context->boot_confirmation.health.pending_slot,
            (unsigned int)context->boot_confirmation.health.boot_state,
            (unsigned int)context->boot_confirmation.health.confirmed,
            (unsigned long)context->boot_confirmation.health.attempts,
            (unsigned long)context->boot_confirmation.health.commits,
            (unsigned long)context->boot_confirmation.health.metadata_sequence,
            error_to_string(context->boot_confirmation.health.last_error));
        break;
    case CLI_COMMAND_FAULT_SHOW:
        {
            fault_record_t record;
            storage_health_t storage_health;
            const char *source = "rtc";
            status_t record_status = fault_recorder_get(
                &context->fault_recorder, &record);

            memset(&storage_health, 0, sizeof(storage_health));
            app_critical_enter(context);
            (void)storage_subsystem_get_health(&context->storage,
                                               &storage_health);
            if (record_status != SYS_OK) {
                record_status = storage_subsystem_load_latest_fault(
                    &context->storage, &record);
                source = "w25";
            }
            app_critical_exit(context);
            if (record_status == SYS_OK) {
                (void)snprintf(response, capacity,
                    "source=%s seq=%lu origin=%s reset=0x%08lX "
                    "task=%s/0x%08lX "
                    "pc=0x%08lX lr=0x%08lX xpsr=0x%08lX cfsr=0x%08lX "
                    "hfsr=0x%08lX mmfar=0x%08lX bfar=0x%08lX | "
                    "archive=%u/%lu event=%u stale=0x%08lX latched=%lu "
                    "supervisor=%s",
                    source,
                    (unsigned long)record.sequence,
                    fault_origin_name((fault_origin_t)record.origin),
                    (unsigned long)record.reset_flags,
                    task_name_from_token(record.task_token),
                    (unsigned long)record.task_token,
                    (unsigned long)record.stacked_pc,
                    (unsigned long)record.stacked_lr,
                    (unsigned long)record.stacked_xpsr,
                    (unsigned long)record.cfsr,
                    (unsigned long)record.hfsr,
                    (unsigned long)record.mmfar,
                    (unsigned long)record.bfar,
                    (unsigned int)storage_health.latest_crash_valid,
                    (unsigned long)storage_health.latest_crash_sequence,
                    (bits & SYSTEM_EVENT_FAULT_ACTIVE) != 0u ? 1u : 0u,
                    (unsigned long)context->supervisor.health.stale_task_mask,
                    (unsigned long)context->supervisor.health.latched_faults,
                    error_to_string(context->supervisor.health.last_error));
            } else {
                (void)snprintf(response, capacity,
                    "source=none archive=%u/%lu | event=%u "
                    "stale=0x%08lX latched=%lu "
                    "supervisor=%s drop measure=%lu can=%lu slog=%lu "
                    "salarm=%lu config=%lu nalarm=%lu nctl=%lu",
                    (unsigned int)storage_health.latest_crash_valid,
                    (unsigned long)storage_health.latest_crash_sequence,
                    (bits & SYSTEM_EVENT_FAULT_ACTIVE) != 0u ? 1u : 0u,
                    (unsigned long)context->supervisor.health.stale_task_mask,
                    (unsigned long)context->supervisor.health.latched_faults,
                    error_to_string(context->supervisor.health.last_error),
                    (unsigned long)context->measurement_publish_drops,
                    (unsigned long)context->can_tx_publish_drops,
                    (unsigned long)context->storage_log_publish_drops,
                    (unsigned long)context->storage_alarm_publish_drops,
                    (unsigned long)context->storage_config_publish_drops,
                    (unsigned long)context->network_alarm_publish_drops,
                    (unsigned long)context->network_control_publish_drops);
            }
        }
        break;
    case CLI_COMMAND_FAULT_CLEAR:
        status = fault_recorder_clear(&context->fault_recorder);
        (void)snprintf(response, capacity, "fault cookie clear: %s",
                       error_to_string(status));
        break;
    case CLI_COMMAND_FAULT_INJECT:
        status = fault_recorder_inject(
            &context->fault_recorder,
            command->argument.fault_injection,
            FAULT_INJECTION_CONFIRMATION);
        (void)snprintf(response, capacity, "fault injection=%u status=%s",
            (unsigned int)command->argument.fault_injection,
            error_to_string(status));
        break;
    default:
        status = ERR_UNSUPPORTED;
        (void)snprintf(response, capacity, "%s", error_to_string(status));
        break;
    }
    return status;
}
