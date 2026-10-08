#include "runtime_internal.h"

static app_supervisor_task_context_t supervisor_task_context;
static app_acquisition_task_context_t acquisition_task_context;
static app_data_hub_task_context_t data_hub_task_context;
static app_fieldbus_context_t fieldbus_tasks_context;
static app_network_task_context_t network_task_context;
static app_storage_task_context_t storage_task_context;
static app_ota_task_context_t ota_task_context;
static app_ui_task_context_t ui_task_context;
static app_cli_task_context_t cli_task_context;
static app_config_service_context_t config_service_context;
static app_runtime_support_context_t runtime_support_context;

static void bind_cli_context(app_context_t *app)
{
    cli_task_context.acquisition_timing = &app->acquisition_timing;
    cli_task_context.boot_confirmation = &app->boot_confirmation;
    cli_task_context.can_tx_publish_drops = &app->can_tx_publish_drops;
    cli_task_context.cli = &app->cli;
    cli_task_context.cli_startup_status = &app->cli_startup_status;
    cli_task_context.critical_timing = &app->critical_timing;
    cli_task_context.deep_power = &app->deep_power;
    cli_task_context.fault_recorder = &app->fault_recorder;
    cli_task_context.heartbeat = &app->heartbeat;
    cli_task_context.measurement_publish_drops =
        &app->measurement_publish_drops;
    cli_task_context.network_alarm_publish_drops =
        &app->network_alarm_publish_drops;
    cli_task_context.network_control_publish_drops =
        &app->network_control_publish_drops;
    cli_task_context.ota = &app->ota;
    cli_task_context.power = &app->power;
    cli_task_context.rtos_diagnostics = &app->rtos_diagnostics;
    cli_task_context.snapshot = &app->snapshot;
    cli_task_context.storage = &app->storage;
    cli_task_context.storage_alarm_publish_drops =
        &app->storage_alarm_publish_drops;
    cli_task_context.storage_config_publish_drops =
        &app->storage_config_publish_drops;
    cli_task_context.storage_log_publish_drops =
        &app->storage_log_publish_drops;
    cli_task_context.supervisor = &app->supervisor;
    cli_task_context.watchdog = &app->watchdog;
}

void app_task_contexts_bind(app_context_t *app)
{
    supervisor_task_context.boot_confirmation = &app->boot_confirmation;
    supervisor_task_context.boot_confirmation_queued =
        &app->boot_confirmation_queued;
    supervisor_task_context.deep_power = &app->deep_power;
    supervisor_task_context.heartbeat = &app->heartbeat;
    supervisor_task_context.initialization_mask = &app->initialization_mask;
    supervisor_task_context.power = &app->power;
    supervisor_task_context.supervisor = &app->supervisor;
    supervisor_task_context.watchdog = &app->watchdog;
    acquisition_task_context.acquisition = &app->acquisition;
    acquisition_task_context.acquisition_timing = &app->acquisition_timing;
    acquisition_task_context.initialization_mask = &app->initialization_mask;
    acquisition_task_context.measurement_publish_drops =
        &app->measurement_publish_drops;
    data_hub_task_context.alarm = &app->alarm;
    data_hub_task_context.can_tx_publish_drops = &app->can_tx_publish_drops;
    data_hub_task_context.network_alarm_publish_drops =
        &app->network_alarm_publish_drops;
    data_hub_task_context.relay = &app->relay;
    data_hub_task_context.snapshot = &app->snapshot;
    data_hub_task_context.storage_alarm_publish_drops =
        &app->storage_alarm_publish_drops;
    data_hub_task_context.storage_log_publish_drops =
        &app->storage_log_publish_drops;
    fieldbus_tasks_context.can_bus = &app->can_bus;
    fieldbus_tasks_context.fieldbus = &app->fieldbus;
    fieldbus_tasks_context.measurement_publish_drops =
        &app->measurement_publish_drops;
    fieldbus_tasks_context.modbus_rs485 = &app->modbus_rs485;
    network_task_context.initialization_mask = &app->initialization_mask;
    network_task_context.network = &app->network;
    network_task_context.network_alarm_publish_drops =
        &app->network_alarm_publish_drops;
    network_task_context.network_control_publish_drops =
        &app->network_control_publish_drops;
    network_task_context.network_startup_status = &app->network_startup_status;
    network_task_context.network_transport = &app->network_transport;
    network_task_context.ota_http = &app->ota_http;
    network_task_context.ota_http_timeout_ms = &app->ota_http_timeout_ms;
    storage_task_context.fault_recorder = &app->fault_recorder;
    storage_task_context.initialization_mask = &app->initialization_mask;
    storage_task_context.ota_staging = &app->ota_staging;
    storage_task_context.power = &app->power;
    storage_task_context.storage = &app->storage;
    storage_task_context.storage_config_publish_drops =
        &app->storage_config_publish_drops;
    storage_task_context.storage_log_publish_drops =
        &app->storage_log_publish_drops;
    storage_task_context.storage_startup_status = &app->storage_startup_status;
    ota_task_context.boot_confirmation = &app->boot_confirmation;
    ota_task_context.boot_confirmation_queued = &app->boot_confirmation_queued;
    ota_task_context.ota = &app->ota;
    ota_task_context.ota_manifest_url = &app->ota_manifest_url;
    ota_task_context.supervisor = &app->supervisor;
    ui_task_context.heartbeat = &app->heartbeat;
    ui_task_context.power = &app->power;
    ui_task_context.ui = &app->ui;
    ui_task_context.ui_startup_status = &app->ui_startup_status;
    bind_cli_context(app);
    config_service_context.alarm = &app->alarm;
    config_service_context.config = &app->config;
    runtime_support_context.critical_timing = &app->critical_timing;
    runtime_support_context.power = &app->power;
    runtime_support_context.rtos_diagnostics = &app->rtos_diagnostics;
    app_config_service_bind(&config_service_context);
    runtime_support_context.heartbeat = &app->heartbeat;
    runtime_support_context.snapshot = &app->snapshot;
    app_runtime_support_bind(&runtime_support_context);
}

void *app_task_context_get(gateway_task_id_t task)
{
    switch (task) {
    case GATEWAY_TASK_SUPERVISOR:
        return &supervisor_task_context;
    case GATEWAY_TASK_ACQUISITION:
        return &acquisition_task_context;
    case GATEWAY_TASK_DATA_HUB:
        return &data_hub_task_context;
    case GATEWAY_TASK_MODBUS:
        return &fieldbus_tasks_context;
    case GATEWAY_TASK_CAN:
        return &fieldbus_tasks_context;
    case GATEWAY_TASK_NETWORK:
        return &network_task_context;
    case GATEWAY_TASK_OTA:
        return &ota_task_context;
    case GATEWAY_TASK_STORAGE:
        return &storage_task_context;
    case GATEWAY_TASK_UI:
        return &ui_task_context;
    case GATEWAY_TASK_CLI:
        return &cli_task_context;
    default:
        return 0;
    }
}
