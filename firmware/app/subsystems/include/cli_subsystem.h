#ifndef GATEWAY_CLI_SUBSYSTEM_H
#define GATEWAY_CLI_SUBSYSTEM_H

#include "cli_transport.h"
#include "config_subsystem.h"
#include "fault_recorder.h"
#include "ui_subsystem.h"

#include <stddef.h>
#include <stdint.h>

#define CLI_LINE_CAPACITY 128u
#define CLI_RESPONSE_CAPACITY 640u

typedef enum {
    CLI_COMMAND_HELP = 0,
    CLI_COMMAND_STATUS,
    CLI_COMMAND_RTOS,
    CLI_COMMAND_RTOS_TASK,
    CLI_COMMAND_RTOS_QUEUE,
    CLI_COMMAND_RTOS_RUNTIME,
    CLI_COMMAND_RTOS_TIMING,
    CLI_COMMAND_SENSOR_LIST,
    CLI_COMMAND_ALARM_LIST,
    CLI_COMMAND_ALARM_ACK,
    CLI_COMMAND_MQTT_STATUS,
    CLI_COMMAND_STORAGE_STATUS,
    CLI_COMMAND_CONFIG_SHOW,
    CLI_COMMAND_CONFIG_SET,
    CLI_COMMAND_UI_PAGE,
    CLI_COMMAND_OTA_STATUS,
    CLI_COMMAND_OTA_CHECK,
    CLI_COMMAND_OTA_START,
    CLI_COMMAND_OTA_APPLY,
    CLI_COMMAND_OTA_CANCEL,
    CLI_COMMAND_POWER_STATUS,
    CLI_COMMAND_POWER_STATS,
    CLI_COMMAND_POWER_LOCK,
    CLI_COMMAND_POWER_STOP,
    CLI_COMMAND_POWER_STANDBY,
    CLI_COMMAND_POWER_CANCEL,
    CLI_COMMAND_SLOT_STATUS,
    CLI_COMMAND_FAULT_SHOW,
    CLI_COMMAND_FAULT_CLEAR,
    CLI_COMMAND_FAULT_INJECT
} cli_command_id_t;

typedef struct {
    cli_command_id_t id;
    union {
        uint32_t alarm_event_id;
        config_patch_t config_patch;
        ui_page_id_t ui_page;
        fault_injection_t fault_injection;
        uint32_t power_duration_ms;
    } argument;
} cli_command_t;

typedef status_t (*cli_command_handler_t)(void *context,
                                          const cli_command_t *command,
                                          char *response, size_t capacity);

typedef struct {
    uint32_t lines_received;
    uint32_t commands_executed;
    uint32_t parse_errors;
    uint32_t transport_errors;
    uint32_t overflows;
    status_t last_error;
    uint8_t initialized;
} cli_health_t;

typedef struct {
    cli_transport_t *transport;
    cli_command_handler_t command_handler;
    void *command_context;
    char line[CLI_LINE_CAPACITY];
    size_t line_length;
    cli_health_t health;
} cli_subsystem_t;

status_t cli_subsystem_construct(cli_subsystem_t *subsystem,
                                 cli_transport_t *transport,
                                 cli_command_handler_t command_handler,
                                 void *command_context);
status_t cli_subsystem_start(cli_subsystem_t *subsystem);
status_t cli_subsystem_set_command_handler(
    cli_subsystem_t *subsystem, cli_command_handler_t command_handler,
    void *command_context);
status_t cli_subsystem_process(cli_subsystem_t *subsystem,
                               uint32_t timeout_ms);
status_t cli_parse_command(char *line, cli_command_t *command);
status_t cli_subsystem_get_health(const cli_subsystem_t *subsystem,
                                  cli_health_t *health);

#endif
