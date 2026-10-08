#include "cli_subsystem.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static char *next_token(char **cursor)
{
    char *start;

    while (**cursor == ' ' || **cursor == '\t') {
        (*cursor)++;
    }
    if (**cursor == '\0') {
        return 0;
    }
    start = *cursor;
    while (**cursor != '\0' && **cursor != ' ' && **cursor != '\t') {
        (*cursor)++;
    }
    if (**cursor != '\0') {
        **cursor = '\0';
        (*cursor)++;
    }
    return start;
}

static status_t parse_i32(const char *text, int32_t *value)
{
    char *end;
    long parsed;

    if (text == 0 || value == 0 || *text == '\0') {
        return ERR_INVALID_ARG;
    }
    errno = 0;
    parsed = strtol(text, &end, 0);
    if (errno != 0 || *end != '\0' || parsed < INT32_MIN ||
        parsed > INT32_MAX) {
        return ERR_INVALID_ARG;
    }
    *value = (int32_t)parsed;
    return SYS_OK;
}

static status_t parse_u32(const char *text, uint32_t *value)
{
    char *end;
    unsigned long parsed;

    if (text == 0 || value == 0 || *text == '\0' || *text == '-') {
        return ERR_INVALID_ARG;
    }
    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || *end != '\0' || parsed == 0u || parsed > UINT32_MAX) {
        return ERR_INVALID_ARG;
    }
    *value = (uint32_t)parsed;
    return SYS_OK;
}

static status_t parse_config_field(const char *text, config_field_t *field)
{
    static const struct {
        const char *name;
        config_field_t field;
    } fields[] = {{"high", CONFIG_FIELD_HIGH_THRESHOLD},
                  {"low", CONFIG_FIELD_LOW_THRESHOLD},
                  {"hysteresis", CONFIG_FIELD_HYSTERESIS},
                  {"assert", CONFIG_FIELD_ASSERT_SAMPLES},
                  {"recover", CONFIG_FIELD_RECOVER_SAMPLES},
                  {"high_enable", CONFIG_FIELD_HIGH_ENABLED},
                  {"low_enable", CONFIG_FIELD_LOW_ENABLED},
                  {"relay_on_alarm", CONFIG_FIELD_RELAY_ON_ALARM},
                  {"relay_safe", CONFIG_FIELD_RELAY_SAFE_ENERGIZED}};
    unsigned int i;

    for (i = 0u; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        if (strcmp(text, fields[i].name) == 0) {
            *field = fields[i].field;
            return SYS_OK;
        }
    }
    return ERR_UNSUPPORTED;
}

static status_t parse_page(const char *text, ui_page_id_t *page)
{
    unsigned int i;

    for (i = 0u; i < UI_PAGE_COUNT; ++i) {
        const char *name = ui_page_name((ui_page_id_t)i);
        size_t j;

        for (j = 0u; name[j] != '\0' && text[j] != '\0'; ++j) {
            char a = name[j] >= 'A' && name[j] <= 'Z'
                         ? (char)(name[j] - 'A' + 'a')
                         : name[j];
            if (a != text[j]) {
                break;
            }
        }
        if (name[j] == '\0' && text[j] == '\0') {
            *page = (ui_page_id_t)i;
            return SYS_OK;
        }
    }
    return ERR_UNSUPPORTED;
}

static status_t parse_rtos(char *second, char **cursor, cli_command_t *command)
{
    if (second == 0) {
        command->id = CLI_COMMAND_RTOS;
        return SYS_OK;
    }
    if (strcmp(second, "task") == 0) {
        command->id = CLI_COMMAND_RTOS_TASK;
    } else if (strcmp(second, "queue") == 0) {
        command->id = CLI_COMMAND_RTOS_QUEUE;
    } else if (strcmp(second, "runtime") == 0) {
        command->id = CLI_COMMAND_RTOS_RUNTIME;
    } else if (strcmp(second, "timing") == 0) {
        command->id = CLI_COMMAND_RTOS_TIMING;
    } else {
        return ERR_UNSUPPORTED;
    }
    return next_token(cursor) == 0 ? SYS_OK : ERR_INVALID_ARG;
}

static status_t parse_alarm(char *second, char **cursor, cli_command_t *command)
{
    char *third;

    if (strcmp(second, "list") == 0) {
        command->id = CLI_COMMAND_ALARM_LIST;
        return next_token(cursor) == 0 ? SYS_OK : ERR_INVALID_ARG;
    }
    if (strcmp(second, "ack") == 0) {
        third = next_token(cursor);
        command->id = CLI_COMMAND_ALARM_ACK;
        return parse_u32(third, &command->argument.alarm_event_id) == SYS_OK &&
                       next_token(cursor) == 0
                   ? SYS_OK
                   : ERR_INVALID_ARG;
    }
    return ERR_UNSUPPORTED;
}

static status_t
parse_config(char *second, char **cursor, cli_command_t *command)
{
    char *third;
    char *fourth;
    char *fifth;
    int32_t point_id;
    int32_t value;

    if (strcmp(second, "show") == 0) {
        command->id = CLI_COMMAND_CONFIG_SHOW;
        return next_token(cursor) == 0 ? SYS_OK : ERR_INVALID_ARG;
    }
    if (strcmp(second, "set") == 0) {
        third = next_token(cursor);
        fourth = next_token(cursor);
        fifth = next_token(cursor);
        if (third == 0 || fourth == 0 || fifth == 0 ||
            parse_i32(third, &point_id) != SYS_OK || point_id < 0 ||
            point_id > UINT16_MAX ||
            parse_config_field(fourth, &command->argument.config_patch.field) !=
                SYS_OK ||
            parse_i32(fifth, &value) != SYS_OK || next_token(cursor) != 0) {
            return ERR_INVALID_ARG;
        }
        command->id = CLI_COMMAND_CONFIG_SET;
        command->argument.config_patch.point_id = (uint16_t)point_id;
        command->argument.config_patch.value = value;
        return SYS_OK;
    }
    return ERR_UNSUPPORTED;
}

static status_t parse_ui(char *second, char **cursor, cli_command_t *command)
{
    char *third;

    if (strcmp(second, "page") != 0) {
        return ERR_UNSUPPORTED;
    }
    third = next_token(cursor);
    command->id = CLI_COMMAND_UI_PAGE;
    return third != 0 && next_token(cursor) == 0
               ? parse_page(third, &command->argument.ui_page)
               : ERR_INVALID_ARG;
}

static status_t parse_ota(char *second, char **cursor, cli_command_t *command)
{
    if (strcmp(second, "status") == 0) {
        command->id = CLI_COMMAND_OTA_STATUS;
    } else if (strcmp(second, "check") == 0) {
        command->id = CLI_COMMAND_OTA_CHECK;
    } else if (strcmp(second, "start") == 0) {
        command->id = CLI_COMMAND_OTA_START;
    } else if (strcmp(second, "apply") == 0) {
        command->id = CLI_COMMAND_OTA_APPLY;
    } else if (strcmp(second, "cancel") == 0) {
        command->id = CLI_COMMAND_OTA_CANCEL;
    } else {
        return ERR_UNSUPPORTED;
    }
    return next_token(cursor) == 0 ? SYS_OK : ERR_INVALID_ARG;
}

static status_t parse_power(char *second, char **cursor, cli_command_t *command)
{
    char *third;
    char *fourth;

    if (strcmp(second, "status") == 0) {
        command->id = CLI_COMMAND_POWER_STATUS;
    } else if (strcmp(second, "stats") == 0) {
        command->id = CLI_COMMAND_POWER_STATS;
    } else if (strcmp(second, "lock") == 0) {
        command->id = CLI_COMMAND_POWER_LOCK;
    } else if (strcmp(second, "stop") == 0) {
        third = next_token(cursor);
        fourth = next_token(cursor);
        if (parse_u32(third, &command->argument.power_duration_ms) != SYS_OK ||
            fourth == 0 || strcmp(fourth, "CONFIRM") != 0 ||
            next_token(cursor) != 0) {
            return ERR_INVALID_ARG;
        }
        command->id = CLI_COMMAND_POWER_STOP;
        return SYS_OK;
    } else if (strcmp(second, "standby") == 0) {
        third = next_token(cursor);
        if (third == 0 || strcmp(third, "CONFIRM") != 0 ||
            next_token(cursor) != 0) {
            return ERR_INVALID_ARG;
        }
        command->id = CLI_COMMAND_POWER_STANDBY;
        return SYS_OK;
    } else if (strcmp(second, "cancel") == 0) {
        command->id = CLI_COMMAND_POWER_CANCEL;
    } else {
        return ERR_UNSUPPORTED;
    }
    return next_token(cursor) == 0 ? SYS_OK : ERR_INVALID_ARG;
}

static status_t parse_fault(char *second, char **cursor, cli_command_t *command)
{
    char *third;
    char *fourth;

    if (strcmp(second, "show") == 0) {
        command->id = CLI_COMMAND_FAULT_SHOW;
        return next_token(cursor) == 0 ? SYS_OK : ERR_INVALID_ARG;
    }
    if (strcmp(second, "clear") == 0) {
        command->id = CLI_COMMAND_FAULT_CLEAR;
        return next_token(cursor) == 0 ? SYS_OK : ERR_INVALID_ARG;
    }
    if (strcmp(second, "inject") == 0) {
        third = next_token(cursor);
        fourth = next_token(cursor);
        if (third == 0 || fourth == 0 || strcmp(fourth, "CONFIRM") != 0 ||
            next_token(cursor) != 0) {
            return ERR_INVALID_ARG;
        }
        if (strcmp(third, "hardfault") == 0) {
            command->argument.fault_injection = FAULT_INJECTION_HARDFAULT;
        } else if (strcmp(third, "watchdog") == 0) {
            command->argument.fault_injection = FAULT_INJECTION_WATCHDOG;
        } else {
            return ERR_UNSUPPORTED;
        }
        command->id = CLI_COMMAND_FAULT_INJECT;
        return SYS_OK;
    }
    return ERR_UNSUPPORTED;
}

typedef struct {
    const char *first;
    const char *second;
    cli_command_id_t id;
} simple_cli_command_t;

static const simple_cli_command_t simple_commands[] = {
    {"help", 0, CLI_COMMAND_HELP},
    {"?", 0, CLI_COMMAND_HELP},
    {"status", 0, CLI_COMMAND_STATUS},
    {"sensor", "list", CLI_COMMAND_SENSOR_LIST},
    {"mqtt", "status", CLI_COMMAND_MQTT_STATUS},
    {"storage", "status", CLI_COMMAND_STORAGE_STATUS},
    {"slot", "status", CLI_COMMAND_SLOT_STATUS}};

static const struct {
    const char *name;
    status_t (*parse)(char *second, char **cursor, cli_command_t *command);
} command_parsers[] = {
    {"rtos", parse_rtos},
    {"alarm", parse_alarm},
    {"config", parse_config},
    {"ui", parse_ui},
    {"ota", parse_ota},
    {"power", parse_power},
    {"fault", parse_fault},
};

status_t cli_parse_command(char *line, cli_command_t *command)
{
    char *cursor = line;
    char *first;
    char *second;
    size_t i;

    if (line == 0 || command == 0) {
        return ERR_INVALID_ARG;
    }
    memset(command, 0, sizeof(*command));
    first = next_token(&cursor);
    if (first == 0) {
        return ERR_INVALID_ARG;
    }
    second = next_token(&cursor);
    for (i = 0u; i < sizeof(simple_commands) / sizeof(simple_commands[0]);
         ++i) {
        const simple_cli_command_t *entry = &simple_commands[i];
        if (strcmp(first, entry->first) != 0) {
            continue;
        }
        if (entry->second == 0) {
            command->id = entry->id;
            return second == 0 ? SYS_OK : ERR_INVALID_ARG;
        }
        if (second != 0 && strcmp(second, entry->second) == 0) {
            command->id = entry->id;
            return next_token(&cursor) == 0 ? SYS_OK : ERR_INVALID_ARG;
        }
        return ERR_UNSUPPORTED;
    }
    for (i = 0u; i < sizeof(command_parsers) / sizeof(command_parsers[0]);
         ++i) {
        if (strcmp(first, command_parsers[i].name) == 0) {
            if (second == 0 && strcmp(first, "rtos") != 0) {
                return ERR_UNSUPPORTED;
            }
            return command_parsers[i].parse(second, &cursor, command);
        }
    }
    return ERR_UNSUPPORTED;
}

static status_t write_text(cli_subsystem_t *subsystem, const char *text)
{
    return cli_transport_write(
        subsystem->transport, (const uint8_t *)text, strlen(text), 1000u);
}

static status_t execute_line(cli_subsystem_t *subsystem)
{
    cli_command_t command;
    char response[CLI_RESPONSE_CAPACITY];
    status_t status;

    subsystem->line[subsystem->line_length] = '\0';
    status = cli_parse_command(subsystem->line, &command);
    if (status != SYS_OK) {
        subsystem->health.parse_errors++;
        (void)write_text(subsystem, "ERR invalid command; type help\r\n> ");
        return status;
    }
    memset(response, 0, sizeof(response));
    status = subsystem->command_handler(
        subsystem->command_context, &command, response, sizeof(response));
    response[sizeof(response) - 1u] = '\0';
    subsystem->health.commands_executed++;
    if (response[0] != '\0') {
        (void)write_text(subsystem, response);
    }
    (void)write_text(subsystem, status == SYS_OK ? "\r\n> " : "\r\nERR\r\n> ");
    return status;
}

status_t cli_subsystem_construct(cli_subsystem_t *subsystem,
                                 cli_transport_t *transport,
                                 cli_command_handler_t command_handler,
                                 void *command_context)
{
    if (subsystem == 0 || transport == 0) {
        return ERR_INVALID_ARG;
    }
    memset(subsystem, 0, sizeof(*subsystem));
    subsystem->transport = transport;
    subsystem->command_handler = command_handler;
    subsystem->command_context = command_context;
    subsystem->health.last_error = ERR_DEVICE_NOT_READY;
    return SYS_OK;
}

status_t cli_subsystem_start(cli_subsystem_t *subsystem)
{
    status_t status;

    if (subsystem == 0 || subsystem->command_handler == 0) {
        return ERR_INVALID_ARG;
    }
    status = cli_transport_init(subsystem->transport);
    if (status == SYS_OK) {
        status = write_text(
            subsystem, "\r\nIndustrial Gateway CLI ready; type help\r\n> ");
    }
    subsystem->health.initialized = status == SYS_OK ? 1u : 0u;
    subsystem->health.last_error = status;
    return status;
}

status_t
cli_subsystem_set_command_handler(cli_subsystem_t *subsystem,
                                  cli_command_handler_t command_handler,
                                  void *command_context)
{
    if (subsystem == 0 || command_handler == 0) {
        return ERR_INVALID_ARG;
    }
    subsystem->command_handler = command_handler;
    subsystem->command_context = command_context;
    return SYS_OK;
}

status_t cli_subsystem_process(cli_subsystem_t *subsystem, uint32_t timeout_ms)
{
    uint8_t data[32];
    size_t length = 0u;
    size_t i;
    status_t status;

    if (subsystem == 0 || subsystem->health.initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    status = cli_transport_read(
        subsystem->transport, data, sizeof(data), &length, timeout_ms);
    if (status == ERR_TIMEOUT) {
        return SYS_OK;
    }
    if (status != SYS_OK) {
        subsystem->health.transport_errors++;
        subsystem->health.last_error = status;
        return status;
    }
    for (i = 0u; i < length; ++i) {
        char ch = (char)data[i];

        if (subsystem->discard_line != 0u) {
            if (ch == '\r' || ch == '\n') {
                subsystem->discard_line = 0u;
            }
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            if (subsystem->line_length != 0u) {
                subsystem->health.lines_received++;
                status = execute_line(subsystem);
                subsystem->line_length = 0u;
                if (status != SYS_OK) {
                    subsystem->health.last_error = status;
                }
            }
        } else if (ch == '\b' || ch == 0x7f) {
            if (subsystem->line_length != 0u) {
                subsystem->line_length--;
            }
        } else if (ch >= 0x20 && ch <= 0x7e) {
            if (subsystem->line_length + 1u < sizeof(subsystem->line)) {
                subsystem->line[subsystem->line_length++] = ch;
            } else {
                subsystem->line_length = 0u;
                subsystem->discard_line = 1u;
                subsystem->health.overflows++;
                (void)write_text(subsystem, "\r\nERR line too long\r\n> ");
            }
        }
    }
    return SYS_OK;
}

status_t cli_subsystem_get_health(const cli_subsystem_t *subsystem,
                                  cli_health_t *health)
{
    if (subsystem == 0 || health == 0) {
        return ERR_INVALID_ARG;
    }
    *health = subsystem->health;
    return SYS_OK;
}
