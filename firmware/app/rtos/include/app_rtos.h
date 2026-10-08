#ifndef GATEWAY_APP_RTOS_H
#define GATEWAY_APP_RTOS_H

#include "app_context.h"

status_t app_rtos_start(app_context_t *context);
status_t app_rtos_publish_measurement(const gateway_measurement_t *measurement);
status_t app_rtos_submit_can_message(const gateway_can_tx_message_t *message);
status_t app_rtos_submit_ota_command(const gateway_ota_command_t *command);
status_t
app_rtos_submit_config(const gateway_storage_config_request_t *request);
status_t app_rtos_submit_ui_page(ui_page_id_t page);
status_t app_rtos_submit_network_control(
    const gateway_network_control_request_t *request);

#endif
