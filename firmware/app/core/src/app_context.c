#include "app_context.h"

#include <string.h>

status_t app_context_init(app_context_t *context)
{
    if (context == 0) {
        return ERR_INVALID_ARG;
    }
    memset(context, 0, sizeof(*context));
    context->acquisition_startup_status = ERR_DEVICE_NOT_READY;
    context->fieldbus_startup_status = ERR_DEVICE_NOT_READY;
    context->storage_startup_status = ERR_DEVICE_NOT_READY;
    context->control_startup_status = ERR_DEVICE_NOT_READY;
    context->network_startup_status = ERR_DEVICE_NOT_READY;
    context->ui_startup_status = ERR_DEVICE_NOT_READY;
    context->cli_startup_status = ERR_DEVICE_NOT_READY;
    context->snapshot.latest.quality = GATEWAY_QUALITY_UNAVAILABLE;
    return SYS_OK;
}

status_t app_context_configure_acquisition(
    app_context_t *context, const app_acquisition_config_t *config)
{
    status_t status;

    if (context == 0 || config == 0) {
        return ERR_INVALID_ARG;
    }
    context->initialization_mask &= ~APP_INITIALIZED_ACQUISITION;
    status = i2c_bus_construct(&context->acquisition_i2c,
                               config->i2c_ops, config->i2c_context,
                               config->i2c_timeout_ms);
    if (status != SYS_OK) {
        return status;
    }
    status = spi_device_construct(&context->max31865_spi,
                                  config->max31865_spi_ops,
                                  config->max31865_spi_context,
                                  config->spi_timeout_ms);
    if (status != SYS_OK) {
        return status;
    }
    status = ads1115_construct(&context->ads1115,
                               &context->acquisition_i2c,
                               &config->ads1115);
    if (status != SYS_OK) {
        return status;
    }
    status = max31865_construct(&context->max31865,
                                &context->max31865_spi,
                                &config->max31865);
    if (status != SYS_OK) {
        return status;
    }
    status = sht30_construct(&context->sht30,
                             &context->acquisition_i2c,
                             &config->sht30);
    if (status != SYS_OK) {
        return status;
    }
    status = acquisition_subsystem_construct(&context->acquisition,
                                             &context->ads1115,
                                             &context->max31865,
                                             &context->sht30,
                                             &config->schedule);
    if (status != SYS_OK) {
        return status;
    }
    status = acquisition_subsystem_start(&context->acquisition);
    context->initialization_mask |= APP_INITIALIZED_ACQUISITION;
    return status;
}

status_t app_context_configure_fieldbus(
    app_context_t *context, const app_fieldbus_config_t *config)
{
    status_t status;

    if (context == 0 || config == 0) {
        return ERR_INVALID_ARG;
    }
    context->initialization_mask &= ~APP_INITIALIZED_FIELDBUS;
    status = rs485_bus_construct(&context->modbus_rs485,
                                 config->rs485_ops,
                                 config->rs485_context,
                                 config->modbus_timeout_ms);
    if (status != SYS_OK) {
        return status;
    }
    status = modbus_master_construct(&context->modbus_master,
                                     &context->modbus_rs485,
                                     config->modbus_poll_table,
                                     config->modbus_poll_count,
                                     config->modbus_retry_limit);
    if (status != SYS_OK) {
        return status;
    }
    status = can_bus_construct(&context->can_bus, config->can_ops,
                               config->can_context);
    if (status != SYS_OK) {
        return status;
    }
    status = fieldbus_subsystem_construct(&context->fieldbus,
                                          &context->modbus_master,
                                          &context->can_bus,
                                          config->local_can_node_id,
                                          config->can_recovery_delay_ms);
    if (status != SYS_OK) {
        return status;
    }
    context->initialization_mask |= APP_INITIALIZED_FIELDBUS;
    return fieldbus_subsystem_start(&context->fieldbus);
}

status_t app_context_configure_control_storage(
    app_context_t *context, const app_control_storage_config_t *config)
{
    gateway_runtime_config_t runtime_config;
    relay_config_t relay_config;
    status_t status;

    if (context == 0 || config == 0) {
        return ERR_INVALID_ARG;
    }
    context->initialization_mask &=
        ~(APP_INITIALIZED_STORAGE | APP_INITIALIZED_CONTROL |
          APP_INITIALIZED_CONFIG);
    status = spi_device_construct(&context->storage_spi,
                                  config->storage_spi_ops,
                                  config->storage_spi_context,
                                  config->storage_spi_timeout_ms);
    if (status != SYS_OK) {
        return status;
    }
    status = w25q128_construct(&context->w25q128, &context->storage_spi,
                               &config->w25q128);
    if (status != SYS_OK) {
        return status;
    }
    status = storage_media_construct(
        &context->storage_media, w25q128_storage_media_ops(),
        &context->w25q128, config->w25q128.total_size,
        EXTERNAL_FLASH_PAGE_SIZE, EXTERNAL_FLASH_SECTOR_SIZE);
    if (status != SYS_OK) {
        return status;
    }
    status = storage_subsystem_construct(
        &context->storage, &context->storage_media,
        &config->default_runtime_config);
    if (status != SYS_OK) {
        return status;
    }
    status = ota_staging_construct(&context->ota_staging,
                                   &context->storage_media);
    if (status != SYS_OK) {
        return status;
    }
    context->initialization_mask |= APP_INITIALIZED_STORAGE;
    context->storage_startup_status =
        storage_subsystem_start(&context->storage);
    runtime_config = config->default_runtime_config;
    if (context->storage_startup_status == SYS_OK) {
        (void)storage_subsystem_load_config(&context->storage,
                                            &runtime_config);
    }

    relay_config = config->relay;
    relay_config.safe_state = runtime_config.relay_safe_energized != 0u
        ? RELAY_ENERGIZED : RELAY_DEENERGIZED;
    status = relay_construct(&context->relay, config->relay_ops,
                             config->relay_context, &relay_config);
    if (status != SYS_OK) {
        return status;
    }
    status = alarm_subsystem_construct(&context->alarm, &context->relay,
                                       &runtime_config);
    if (status != SYS_OK) {
        return status;
    }
    status = config_subsystem_construct(&context->config, &runtime_config);
    if (status != SYS_OK) {
        return status;
    }
    context->initialization_mask |= APP_INITIALIZED_CONFIG;
    context->control_startup_status = alarm_subsystem_start(&context->alarm);
    if (context->control_startup_status != SYS_OK) {
        return context->control_startup_status;
    }
    context->initialization_mask |= APP_INITIALIZED_CONTROL;
    return context->storage_startup_status;
}

status_t app_context_configure_network(
    app_context_t *context, const app_network_config_t *config)
{
    status_t status;
    size_t manifest_url_length;

    if (context == 0 || config == 0 || config->ota_manifest_url == 0 ||
        config->ota_http_timeout_ms == 0u) {
        return ERR_INVALID_ARG;
    }
    manifest_url_length = strlen(config->ota_manifest_url);
    if (manifest_url_length == 0u ||
        manifest_url_length >= sizeof(context->ota_manifest_url)) {
        return ERR_INVALID_ARG;
    }
    context->initialization_mask &= ~APP_INITIALIZED_NETWORK;
    status = esp8266_construct(&context->esp8266, config->serial_ops,
                               config->serial_context, &config->esp8266);
    if (status != SYS_OK) {
        return status;
    }
    status = network_transport_construct(
        &context->network_transport, esp8266_network_transport_ops(),
        &context->esp8266);
    if (status != SYS_OK) {
        return status;
    }
    status = network_subsystem_construct(&context->network,
                                         &context->network_transport,
                                         &config->network);
    if (status != SYS_OK) {
        return status;
    }
    memcpy(context->ota_manifest_url, config->ota_manifest_url,
           manifest_url_length + 1u);
    context->ota_http_timeout_ms = config->ota_http_timeout_ms;
    context->initialization_mask |= APP_INITIALIZED_NETWORK;
    return SYS_OK;
}

status_t app_context_configure_ui(app_context_t *context,
                                  const app_ui_config_t *config)
{
    status_t status;

    if (context == 0 || config == 0) {
        return ERR_INVALID_ARG;
    }
    context->initialization_mask &= ~APP_INITIALIZED_UI;
    status = display_device_construct(&context->display,
                                      config->display_ops,
                                      config->display_context,
                                      config->width, config->height);
    if (status == SYS_OK && config->input_ops != 0) {
        status = input_device_construct(&context->input,
                                        config->input_ops,
                                        config->input_context,
                                        config->width, config->height);
    }
    if (status == SYS_OK) {
        status = ui_subsystem_construct(&context->ui, &context->display,
                                        config->input_ops != 0
                                            ? &context->input : 0,
                                        0, 0);
    }
    if (status == SYS_OK) {
        status = ui_subsystem_configure_draw_buffers(
            &context->ui, config->draw_buffer_primary,
            config->draw_buffer_secondary, config->draw_buffer_pixels,
            config->draw_buffer_degraded);
    }
    if (status == SYS_OK) {
        context->initialization_mask |= APP_INITIALIZED_UI;
    }
    return status;
}

status_t app_context_configure_cli(app_context_t *context,
                                   const app_cli_config_t *config)
{
    status_t status;

    if (context == 0 || config == 0) {
        return ERR_INVALID_ARG;
    }
    context->initialization_mask &= ~APP_INITIALIZED_CLI;
    status = cli_transport_construct(&context->cli_transport,
                                     config->transport_ops,
                                     config->transport_context);
    if (status == SYS_OK) {
        status = cli_subsystem_construct(&context->cli,
                                         &context->cli_transport, 0, 0);
    }
    if (status == SYS_OK) {
        context->initialization_mask |= APP_INITIALIZED_CLI;
    }
    return status;
}

status_t app_context_configure_reliability(
    app_context_t *context, const app_reliability_config_t *config)
{
    status_t status;

    if (context == 0 || config == 0) {
        return ERR_INVALID_ARG;
    }
    context->initialization_mask &= ~APP_INITIALIZED_RELIABILITY;
    status = watchdog_device_construct(&context->watchdog,
                                       config->watchdog_ops,
                                       config->watchdog_context);
    if (status == SYS_OK) {
        status = fault_recorder_construct(
            &context->fault_recorder, config->fault_recorder_ops,
            config->fault_recorder_context,
            config->fault_injection_enabled);
    }
    if (status == SYS_OK) {
        status = power_manager_construct(&context->power, &config->power, 0u);
    }
    if (status == SYS_OK) {
        status = deep_power_controller_construct(
            &context->deep_power, config->deep_power_ops,
            config->deep_power_context, &config->deep_power);
    }
    if (status == SYS_OK) {
        status = boot_confirmation_construct(&context->boot_confirmation,
                                              &config->metadata_store,
                                              config->running_slot);
    }
    if (status == SYS_OK) {
        status = watchdog_device_start(&context->watchdog,
                                       config->watchdog_timeout_ms);
    }
    if (status == SYS_OK) {
        status = supervisor_subsystem_construct(&context->supervisor,
                                                &context->watchdog,
                                                &config->supervisor);
    }
    if (status == SYS_OK) {
        context->initialization_mask |= APP_INITIALIZED_RELIABILITY;
    }
    return status;
}

void app_context_mark_alive(app_context_t *context, gateway_task_id_t task)
{
    if (context != 0 && (unsigned int)task < GATEWAY_TASK_COUNT) {
        context->heartbeat[(unsigned int)task]++;
    }
}
