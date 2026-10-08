#ifndef GATEWAY_APP_CONTEXT_H
#define GATEWAY_APP_CONTEXT_H

#include "error_code.h"
#include "gateway_model.h"
#include "acquisition_subsystem.h"
#include "alarm_subsystem.h"
#include "boot_confirmation.h"
#include "can_bus.h"
#include "cli_subsystem.h"
#include "cli_transport.h"
#include "config_subsystem.h"
#include "critical_timing_monitor.h"
#include "deep_power_controller.h"
#include "display_device.h"
#include "fieldbus_subsystem.h"
#include "fault_recorder.h"
#include "http_client_raw.h"
#include "periodic_timing_monitor.h"
#include "esp8266.h"
#include "i2c_bus.h"
#include "input_device.h"
#include "modbus_rtu_master.h"
#include "network_subsystem.h"
#include "network_transport.h"
#include "ota_manager.h"
#include "ota_staging.h"
#include "power_manager.h"
#include "relay.h"
#include "rs485_bus.h"
#include "spi_bus.h"
#include "storage_media.h"
#include "storage_subsystem.h"
#include "supervisor_subsystem.h"
#include "ui_subsystem.h"
#include "w25q128.h"
#include "watchdog_device.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    GATEWAY_TASK_SUPERVISOR = 0,
    GATEWAY_TASK_ACQUISITION,
    GATEWAY_TASK_DATA_HUB,
    GATEWAY_TASK_MODBUS,
    GATEWAY_TASK_CAN,
    GATEWAY_TASK_NETWORK,
    GATEWAY_TASK_OTA,
    GATEWAY_TASK_STORAGE,
    GATEWAY_TASK_UI,
    GATEWAY_TASK_CLI,
    GATEWAY_TASK_COUNT
} gateway_task_id_t;

enum {
    APP_INITIALIZED_ACQUISITION = 1UL << 0,
    APP_INITIALIZED_FIELDBUS = 1UL << 1,
    APP_INITIALIZED_STORAGE = 1UL << 2,
    APP_INITIALIZED_CONTROL = 1UL << 3,
    APP_INITIALIZED_NETWORK = 1UL << 4,
    APP_INITIALIZED_UI = 1UL << 5,
    APP_INITIALIZED_CLI = 1UL << 6,
    APP_INITIALIZED_CONFIG = 1UL << 7,
    APP_INITIALIZED_RELIABILITY = 1UL << 8
};

typedef struct {
    const i2c_bus_ops_t *i2c_ops;
    void *i2c_context;
    uint32_t i2c_timeout_ms;
    const spi_device_ops_t *max31865_spi_ops;
    void *max31865_spi_context;
    uint32_t spi_timeout_ms;
    ads1115_config_t ads1115;
    max31865_config_t max31865;
    sht30_config_t sht30;
    acquisition_schedule_t schedule;
} app_acquisition_config_t;

typedef struct {
    const rs485_bus_ops_t *rs485_ops;
    void *rs485_context;
    uint32_t modbus_timeout_ms;
    const modbus_poll_entry_t *modbus_poll_table;
    size_t modbus_poll_count;
    uint8_t modbus_retry_limit;
    const can_bus_ops_t *can_ops;
    void *can_context;
    uint8_t local_can_node_id;
    uint32_t can_recovery_delay_ms;
} app_fieldbus_config_t;

typedef struct {
    const spi_device_ops_t *storage_spi_ops;
    void *storage_spi_context;
    uint32_t storage_spi_timeout_ms;
    w25q128_config_t w25q128;
    const relay_ops_t *relay_ops;
    void *relay_context;
    relay_config_t relay;
    gateway_runtime_config_t default_runtime_config;
} app_control_storage_config_t;

typedef struct {
    const esp8266_serial_ops_t *serial_ops;
    void *serial_context;
    esp8266_config_t esp8266;
    network_subsystem_config_t network;
    const char *ota_manifest_url;
    uint32_t ota_http_timeout_ms;
} app_network_config_t;

typedef struct {
    const display_device_ops_t *display_ops;
    void *display_context;
    const input_device_ops_t *input_ops;
    void *input_context;
    uint16_t width;
    uint16_t height;
    uint16_t *draw_buffer_primary;
    uint16_t *draw_buffer_secondary;
    size_t draw_buffer_pixels;
    uint8_t draw_buffer_degraded;
} app_ui_config_t;

typedef struct {
    const cli_transport_ops_t *transport_ops;
    void *transport_context;
} app_cli_config_t;

typedef struct {
    const watchdog_device_ops_t *watchdog_ops;
    void *watchdog_context;
    const fault_recorder_ops_t *fault_recorder_ops;
    void *fault_recorder_context;
    uint8_t fault_injection_enabled;
    uint32_t watchdog_timeout_ms;
    boot_meta_store_t metadata_store;
    app_slot_t running_slot;
    power_manager_config_t power;
    const deep_power_platform_ops_t *deep_power_ops;
    void *deep_power_context;
    deep_power_controller_config_t deep_power;
    supervisor_config_t supervisor;
} app_reliability_config_t;

typedef struct {
    uint32_t stack_high_water[GATEWAY_TASK_COUNT];
    uint16_t cpu_permille[GATEWAY_TASK_COUNT];
    uint16_t idle_cpu_permille;
    uint16_t system_cpu_permille;
    uint16_t queue_current[15];
    uint16_t queue_high_water[15];
    uint32_t samples;
    uint32_t runtime_samples;
    uint32_t runtime_errors;
} app_rtos_diagnostics_t;

typedef struct {
    uint32_t heartbeat[GATEWAY_TASK_COUNT];
    uint32_t initialization_mask;
    uint32_t measurement_publish_drops;
    uint32_t can_tx_publish_drops;
    uint32_t storage_log_publish_drops;
    uint32_t storage_alarm_publish_drops;
    uint32_t storage_config_publish_drops;
    uint32_t network_alarm_publish_drops;
    uint32_t network_control_publish_drops;
    volatile uint8_t boot_confirmation_queued;
    status_t acquisition_startup_status;
    status_t fieldbus_startup_status;
    status_t storage_startup_status;
    status_t control_startup_status;
    status_t network_startup_status;
    status_t ui_startup_status;
    status_t cli_startup_status;
    gateway_system_snapshot_t snapshot;
    i2c_bus_t acquisition_i2c;
    spi_device_t max31865_spi;
    ads1115_t ads1115;
    max31865_t max31865;
    sht30_t sht30;
    acquisition_subsystem_t acquisition;
    rs485_bus_t modbus_rs485;
    modbus_master_t modbus_master;
    can_bus_t can_bus;
    fieldbus_subsystem_t fieldbus;
    spi_device_t storage_spi;
    w25q128_t w25q128;
    storage_media_t storage_media;
    storage_subsystem_t storage;
    ota_staging_t ota_staging;
    relay_t relay;
    alarm_subsystem_t alarm;
    config_subsystem_t config;
    esp8266_t esp8266;
    network_transport_t network_transport;
    network_subsystem_t network;
    http_client_raw_t ota_http;
    ota_manager_t ota;
    char ota_manifest_url[MANIFEST_DOWNLOAD_URL_LEN];
    uint32_t ota_http_timeout_ms;
    display_device_t display;
    input_device_t input;
    ui_subsystem_t ui;
    cli_transport_t cli_transport;
    cli_subsystem_t cli;
    watchdog_device_t watchdog;
    fault_recorder_t fault_recorder;
    power_manager_t power;
    deep_power_controller_t deep_power;
    supervisor_subsystem_t supervisor;
    boot_confirmation_t boot_confirmation;
    periodic_timing_monitor_t acquisition_timing;
    critical_timing_monitor_t critical_timing;
    app_rtos_diagnostics_t rtos_diagnostics;
} app_context_t;

status_t app_context_init(app_context_t *context);
status_t
app_context_configure_acquisition(app_context_t *context,
                                  const app_acquisition_config_t *config);
status_t app_context_configure_fieldbus(app_context_t *context,
                                        const app_fieldbus_config_t *config);
status_t app_context_configure_control_storage(
    app_context_t *context, const app_control_storage_config_t *config);
status_t app_context_configure_network(app_context_t *context,
                                       const app_network_config_t *config);
status_t app_context_configure_ui(app_context_t *context,
                                  const app_ui_config_t *config);
status_t app_context_configure_cli(app_context_t *context,
                                   const app_cli_config_t *config);
status_t
app_context_configure_reliability(app_context_t *context,
                                  const app_reliability_config_t *config);
void app_context_mark_alive(app_context_t *context, gateway_task_id_t task);

#endif
