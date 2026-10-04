#ifndef APP_TASK_CONTEXTS_H
#define APP_TASK_CONTEXTS_H

#include "app_context.h"

typedef struct app_supervisor_task_context {
    boot_confirmation_t *boot_confirmation;
    volatile uint8_t *boot_confirmation_queued;
    deep_power_controller_t *deep_power;
    uint32_t (*heartbeat)[GATEWAY_TASK_COUNT];
    uint32_t *initialization_mask;
    power_manager_t *power;
    supervisor_subsystem_t *supervisor;
    watchdog_device_t *watchdog;
} app_supervisor_task_context_t;

typedef struct app_acquisition_task_context {
    acquisition_subsystem_t *acquisition;
    periodic_timing_monitor_t *acquisition_timing;
    uint32_t *initialization_mask;
    uint32_t *measurement_publish_drops;
} app_acquisition_task_context_t;

typedef struct app_data_hub_task_context {
    alarm_subsystem_t *alarm;
    uint32_t *can_tx_publish_drops;
    uint32_t *network_alarm_publish_drops;
    relay_t *relay;
    gateway_system_snapshot_t *snapshot;
    uint32_t *storage_alarm_publish_drops;
    uint32_t *storage_log_publish_drops;
} app_data_hub_task_context_t;

typedef struct app_fieldbus_context {
    can_bus_t *can_bus;
    fieldbus_subsystem_t *fieldbus;
    uint32_t *measurement_publish_drops;
    rs485_bus_t *modbus_rs485;
} app_fieldbus_context_t;

typedef struct app_network_task_context {
    uint32_t *initialization_mask;
    network_subsystem_t *network;
    uint32_t *network_alarm_publish_drops;
    uint32_t *network_control_publish_drops;
    status_t *network_startup_status;
    network_transport_t *network_transport;
    http_client_raw_t *ota_http;
    uint32_t *ota_http_timeout_ms;
} app_network_task_context_t;

typedef struct app_storage_task_context {
    fault_recorder_t *fault_recorder;
    uint32_t *initialization_mask;
    ota_staging_t *ota_staging;
    power_manager_t *power;
    storage_subsystem_t *storage;
    uint32_t *storage_config_publish_drops;
    uint32_t *storage_log_publish_drops;
    status_t *storage_startup_status;
} app_storage_task_context_t;

typedef struct app_ota_task_context {
    boot_confirmation_t *boot_confirmation;
    volatile uint8_t *boot_confirmation_queued;
    ota_manager_t *ota;
    char (*ota_manifest_url)[MANIFEST_DOWNLOAD_URL_LEN];
    supervisor_subsystem_t *supervisor;
} app_ota_task_context_t;

typedef struct app_ui_task_context {
    uint32_t (*heartbeat)[GATEWAY_TASK_COUNT];
    power_manager_t *power;
    ui_subsystem_t *ui;
    status_t *ui_startup_status;
} app_ui_task_context_t;

typedef struct app_cli_task_context {
    periodic_timing_monitor_t *acquisition_timing;
    boot_confirmation_t *boot_confirmation;
    uint32_t *can_tx_publish_drops;
    cli_subsystem_t *cli;
    status_t *cli_startup_status;
    critical_timing_monitor_t *critical_timing;
    deep_power_controller_t *deep_power;
    fault_recorder_t *fault_recorder;
    uint32_t (*heartbeat)[GATEWAY_TASK_COUNT];
    uint32_t *measurement_publish_drops;
    uint32_t *network_alarm_publish_drops;
    uint32_t *network_control_publish_drops;
    ota_manager_t *ota;
    power_manager_t *power;
    app_rtos_diagnostics_t *rtos_diagnostics;
    gateway_system_snapshot_t *snapshot;
    storage_subsystem_t *storage;
    uint32_t *storage_alarm_publish_drops;
    uint32_t *storage_config_publish_drops;
    uint32_t *storage_log_publish_drops;
    supervisor_subsystem_t *supervisor;
    watchdog_device_t *watchdog;
} app_cli_task_context_t;

typedef struct app_config_service_context {
    alarm_subsystem_t *alarm;
    config_subsystem_t *config;
} app_config_service_context_t;

typedef struct app_runtime_support_context {
    gateway_system_snapshot_t *snapshot;
    uint32_t (*heartbeat)[GATEWAY_TASK_COUNT];
    critical_timing_monitor_t *critical_timing;
    power_manager_t *power;
    app_rtos_diagnostics_t *rtos_diagnostics;
} app_runtime_support_context_t;

void app_task_contexts_bind(app_context_t *app);
void *app_task_context_get(gateway_task_id_t task);
void app_config_service_bind(app_config_service_context_t *context);
void app_runtime_support_bind(app_runtime_support_context_t *context);
#endif
