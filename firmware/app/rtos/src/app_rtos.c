#include "runtime_internal.h"
#include <string.h>

typedef struct {
    uint8_t
        measurement[MEASUREMENT_QUEUE_LENGTH * sizeof(gateway_measurement_t)];
    uint8_t can_tx[CAN_TX_QUEUE_LENGTH * sizeof(gateway_can_tx_message_t)];
    uint8_t ui_snapshot[UI_SNAPSHOT_QUEUE_LENGTH *
                        sizeof(gateway_system_snapshot_t)];
    uint8_t network_telemetry[NETWORK_TELEMETRY_QUEUE_LENGTH *
                              sizeof(gateway_network_event_t)];
    uint8_t network_alarm[NETWORK_ALARM_QUEUE_LENGTH *
                          sizeof(gateway_network_event_t)];
    uint8_t network_control[NETWORK_CONTROL_QUEUE_LENGTH *
                            sizeof(gateway_network_control_request_t)];
    uint8_t network_control_result[NETWORK_CONTROL_RESULT_QUEUE_LENGTH *
                                   sizeof(gateway_network_control_result_t)];
    uint8_t
        ota_command[OTA_COMMAND_QUEUE_LENGTH * sizeof(gateway_ota_command_t)];
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
    uint8_t power_command[POWER_COMMAND_QUEUE_LENGTH * sizeof(power_command_t)];
    uint8_t
        storage_set[STORAGE_QUEUE_SET_LENGTH * sizeof(QueueSetMemberHandle_t)];
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

app_channel_handles_t channels;
static app_channel_storage_t channel_storage;
static StaticQueue_t queue_control_blocks[15];
static StaticQueue_t queue_set_control_block;
static StaticEventGroup_t system_event_control_block;
static StaticSemaphore_t snapshot_mutex_control_block;
static StaticSemaphore_t config_mutex_control_block;
static StaticTask_t task_control_blocks[GATEWAY_TASK_COUNT];
TaskHandle_t task_handles[GATEWAY_TASK_COUNT];
static app_task_stacks_t task_stacks;

static const app_task_spec_t task_specs[GATEWAY_TASK_COUNT] = {
    {GATEWAY_TASK_SUPERVISOR, "Supervisor", supervisor_task, 6U, 512U},
    {GATEWAY_TASK_ACQUISITION, "Acquisition", acquisition_task, 5U, 768U},
    {GATEWAY_TASK_DATA_HUB, "DataHub", data_hub_task, 5U, 768U},
    {GATEWAY_TASK_MODBUS, "Modbus", modbus_task, 4U, 768U},
    {GATEWAY_TASK_CAN, "CAN", can_task, 4U, 512U},
    {GATEWAY_TASK_NETWORK, "Network", network_task, 4U, 1024U},
    {GATEWAY_TASK_OTA, "OTA", ota_task, 3U, 1536U},
    {GATEWAY_TASK_STORAGE, "Storage", storage_task, 2U, 768U},
    {GATEWAY_TASK_UI, "UI", ui_task, 2U, 1536U},
    {GATEWAY_TASK_CLI, "CLI", cli_task, 1U, 768U}};

const char *app_runtime_task_name(gateway_task_id_t task)
{
    return (unsigned int)task < GATEWAY_TASK_COUNT
               ? task_specs[(unsigned int)task].name
               : "unknown";
}

static StackType_t *task_stack(gateway_task_id_t id)
{
    switch (id) {
    case GATEWAY_TASK_SUPERVISOR:
        return task_stacks.supervisor;
    case GATEWAY_TASK_ACQUISITION:
        return task_stacks.acquisition;
    case GATEWAY_TASK_DATA_HUB:
        return task_stacks.data_hub;
    case GATEWAY_TASK_MODBUS:
        return task_stacks.modbus;
    case GATEWAY_TASK_CAN:
        return task_stacks.can_task;
    case GATEWAY_TASK_NETWORK:
        return task_stacks.network;
    case GATEWAY_TASK_OTA:
        return task_stacks.ota;
    case GATEWAY_TASK_STORAGE:
        return task_stacks.storage;
    case GATEWAY_TASK_UI:
        return task_stacks.ui;
    case GATEWAY_TASK_CLI:
        return task_stacks.cli;
    default:
        return 0;
    }
}

static void create_channel_resources(void)
{
    channels.measurement = xQueueCreateStatic(MEASUREMENT_QUEUE_LENGTH,
                                              sizeof(gateway_measurement_t),
                                              channel_storage.measurement,
                                              &queue_control_blocks[0]);
    channels.can_tx = xQueueCreateStatic(CAN_TX_QUEUE_LENGTH,
                                         sizeof(gateway_can_tx_message_t),
                                         channel_storage.can_tx,
                                         &queue_control_blocks[1]);
    channels.ui_snapshot = xQueueCreateStatic(UI_SNAPSHOT_QUEUE_LENGTH,
                                              sizeof(gateway_system_snapshot_t),
                                              channel_storage.ui_snapshot,
                                              &queue_control_blocks[2]);
    channels.network_telemetry =
        xQueueCreateStatic(NETWORK_TELEMETRY_QUEUE_LENGTH,
                           sizeof(gateway_network_event_t),
                           channel_storage.network_telemetry,
                           &queue_control_blocks[3]);
    channels.network_alarm = xQueueCreateStatic(NETWORK_ALARM_QUEUE_LENGTH,
                                                sizeof(gateway_network_event_t),
                                                channel_storage.network_alarm,
                                                &queue_control_blocks[4]);
    channels.network_control =
        xQueueCreateStatic(NETWORK_CONTROL_QUEUE_LENGTH,
                           sizeof(gateway_network_control_request_t),
                           channel_storage.network_control,
                           &queue_control_blocks[5]);
    channels.network_control_result =
        xQueueCreateStatic(NETWORK_CONTROL_RESULT_QUEUE_LENGTH,
                           sizeof(gateway_network_control_result_t),
                           channel_storage.network_control_result,
                           &queue_control_blocks[6]);
    channels.ota_command = xQueueCreateStatic(OTA_COMMAND_QUEUE_LENGTH,
                                              sizeof(gateway_ota_command_t),
                                              channel_storage.ota_command,
                                              &queue_control_blocks[7]);
    channels.storage_log =
        xQueueCreateStatic(STORAGE_LOG_QUEUE_LENGTH,
                           sizeof(gateway_storage_log_request_t),
                           channel_storage.storage_log,
                           &queue_control_blocks[8]);
    channels.storage_alarm =
        xQueueCreateStatic(STORAGE_ALARM_QUEUE_LENGTH,
                           sizeof(gateway_storage_alarm_request_t),
                           channel_storage.storage_alarm,
                           &queue_control_blocks[9]);
    channels.storage_config =
        xQueueCreateStatic(STORAGE_CONFIG_QUEUE_LENGTH,
                           sizeof(gateway_storage_config_request_t),
                           channel_storage.storage_config,
                           &queue_control_blocks[10]);
    channels.ui_command = xQueueCreateStatic(UI_COMMAND_QUEUE_LENGTH,
                                             sizeof(ui_page_id_t),
                                             channel_storage.ui_command,
                                             &queue_control_blocks[11]);
    channels.ota_network_request =
        xQueueCreateStatic(OTA_NETWORK_REQUEST_QUEUE_LENGTH,
                           sizeof(ota_network_request_t *),
                           channel_storage.ota_network_request,
                           &queue_control_blocks[12]);
    channels.ota_storage_request =
        xQueueCreateStatic(OTA_STORAGE_REQUEST_QUEUE_LENGTH,
                           sizeof(ota_storage_request_t *),
                           channel_storage.ota_storage_request,
                           &queue_control_blocks[13]);
    channels.power_command = xQueueCreateStatic(POWER_COMMAND_QUEUE_LENGTH,
                                                sizeof(power_command_t),
                                                channel_storage.power_command,
                                                &queue_control_blocks[14]);
    channels.storage_set = xQueueCreateSetStatic(STORAGE_QUEUE_SET_LENGTH,
                                                 channel_storage.storage_set,
                                                 &queue_set_control_block);
    channels.system_events =
        xEventGroupCreateStatic(&system_event_control_block);
    channels.snapshot_mutex =
        xSemaphoreCreateMutexStatic(&snapshot_mutex_control_block);
    channels.config_mutex =
        xSemaphoreCreateMutexStatic(&config_mutex_control_block);
}

static status_t validate_channels(void)
{
    if (channels.measurement == 0 || channels.can_tx == 0 ||
        channels.ui_snapshot == 0 || channels.network_telemetry == 0 ||
        channels.network_alarm == 0 || channels.network_control == 0 ||
        channels.network_control_result == 0 || channels.ota_command == 0 ||
        channels.storage_log == 0 || channels.storage_alarm == 0 ||
        channels.storage_config == 0 || channels.ui_command == 0 ||
        channels.ota_network_request == 0 ||
        channels.ota_storage_request == 0 || channels.power_command == 0 ||
        channels.storage_set == 0 || channels.system_events == 0 ||
        channels.snapshot_mutex == 0 || channels.config_mutex == 0) {
        return ERR_NO_MEMORY;
    }
    if (xQueueAddToSet(channels.storage_log, channels.storage_set) != pdPASS ||
        xQueueAddToSet(channels.storage_alarm, channels.storage_set) !=
            pdPASS ||
        xQueueAddToSet(channels.storage_config, channels.storage_set) !=
            pdPASS ||
        xQueueAddToSet(channels.ota_storage_request, channels.storage_set) !=
            pdPASS) {
        return ERR_INVALID_ARG;
    }

    return SYS_OK;
}

static void register_channels(void)
{
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
}

static status_t create_channels(void)
{
    status_t status;
    create_channel_resources();
    status = validate_channels();
    if (status == SYS_OK) {
        register_channels();
    }
    return status;
}

static status_t create_tasks(void)
{
    size_t i;

    for (i = 0u; i < GATEWAY_TASK_COUNT; ++i) {
        const app_task_spec_t *spec = &task_specs[i];
        TaskHandle_t task =
            xTaskCreateStatic(spec->entry,
                              spec->name,
                              spec->stack_words,
                              app_task_context_get(spec->id),
                              spec->priority,
                              task_stack(spec->id),
                              &task_control_blocks[(unsigned int)spec->id]);
        if (task == 0) {
            return ERR_NO_MEMORY;
        }
        task_handles[(unsigned int)spec->id] = task;
    }
    return SYS_OK;
}

status_t app_rtos_start(app_context_t *context)
{
    status_t status;

    if (context == 0) {
        return ERR_INVALID_ARG;
    }
    app_task_contexts_bind(context);
    memset(&channels, 0, sizeof(channels));

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
        xEventGroupSetBits(channels.system_events, SYSTEM_EVENT_FIELDBUS_READY);
    }
    if ((context->initialization_mask & APP_INITIALIZED_CONTROL) != 0u) {
        xEventGroupSetBits(channels.system_events, SYSTEM_EVENT_CONTROL_READY);
    }
    if (context->storage_startup_status == SYS_OK) {
        xEventGroupSetBits(channels.system_events, SYSTEM_EVENT_STORAGE_READY);
    }
    vTaskStartScheduler();
    return ERR_NO_MEMORY;
}

status_t app_rtos_submit_can_message(const gateway_can_tx_message_t *message)
{
    if (message == 0 || channels.can_tx == 0) {
        return ERR_INVALID_ARG;
    }
    return xQueueSend(channels.can_tx, message, 0U) == pdPASS ? SYS_OK
                                                              : ERR_QUEUE_FULL;
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

status_t app_rtos_submit_config(const gateway_storage_config_request_t *request)
{
    if (request == 0 || channels.storage_config == 0) {
        return ERR_INVALID_ARG;
    }
    return xQueueSend(channels.storage_config, request, pdMS_TO_TICKS(5u)) ==
                   pdPASS
               ? SYS_OK
               : ERR_QUEUE_FULL;
}

status_t app_rtos_submit_ui_page(ui_page_id_t page)
{
    if ((unsigned int)page >= UI_PAGE_COUNT || channels.ui_command == 0) {
        return ERR_INVALID_ARG;
    }
    if (channels.system_events == 0 ||
        (xEventGroupGetBits(channels.system_events) & SYSTEM_EVENT_UI_READY) ==
            0u) {
        return ERR_DEVICE_NOT_READY;
    }
    return xQueueSend(channels.ui_command, &page, 0u) == pdPASS
               ? SYS_OK
               : ERR_QUEUE_FULL;
}

status_t app_rtos_submit_network_control(
    const gateway_network_control_request_t *request)
{
    if (request == 0 || channels.network_control == 0) {
        return ERR_INVALID_ARG;
    }
    return xQueueSend(channels.network_control, request, pdMS_TO_TICKS(20u)) ==
                   pdPASS
               ? SYS_OK
               : ERR_QUEUE_FULL;
}
