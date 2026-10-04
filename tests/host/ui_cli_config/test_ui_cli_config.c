#include "alarm_subsystem.h"
#include "cli_subsystem.h"
#include "config_subsystem.h"
#include "display_device.h"
#include "relay.h"
#include "ui_subsystem.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t flush_count;
    uint32_t pixels;
    uint8_t backlight;
    uint8_t suspend_count;
    uint8_t resume_count;
} fake_display_t;

typedef struct {
    input_sample_t sample;
    uint8_t suspend_count;
    uint8_t resume_count;
} fake_input_t;

typedef struct {
    const char *input;
    size_t input_offset;
    char output[2048];
    size_t output_length;
    cli_command_t last_command;
    uint32_t command_count;
} fake_cli_t;

typedef struct {
    int level;
} fake_relay_t;

static gateway_runtime_config_t make_config(void)
{
    gateway_runtime_config_t config;

    memset(&config, 0, sizeof(config));
    config.schema_version = GATEWAY_RUNTIME_CONFIG_SCHEMA_VERSION;
    config.revision = 7u;
    config.rule_count = 1u;
    config.rules[0].point_id = GATEWAY_POINT_LOOP_CURRENT;
    config.rules[0].high_enabled = 1u;
    config.rules[0].low_enabled = 1u;
    config.rules[0].relay_on_alarm = 1u;
    config.rules[0].assert_samples = 1u;
    config.rules[0].recover_samples = 1u;
    config.rules[0].high_threshold = 20000;
    config.rules[0].low_threshold = 4000;
    config.rules[0].hysteresis = 200;
    return config;
}

static status_t fake_display_init(void *context, uint16_t width,
                                  uint16_t height)
{
    return context != 0 && width >= 320u && height >= 240u
        ? SYS_OK : ERR_INVALID_ARG;
}

static status_t fake_display_flush(void *context,
                                   const display_area_t *area,
                                   const uint16_t *pixels,
                                   size_t pixel_count)
{
    fake_display_t *display = context;

    assert(area != 0);
    assert(pixels != 0);
    display->flush_count++;
    display->pixels += (uint32_t)pixel_count;
    return SYS_OK;
}

static status_t fake_display_backlight(void *context, uint8_t percent)
{
    fake_display_t *display = context;

    display->backlight = percent;
    return SYS_OK;
}

static status_t fake_display_suspend(void *context)
{
    fake_display_t *display = context;

    display->backlight = 0u;
    display->suspend_count++;
    return SYS_OK;
}

static status_t fake_display_resume(void *context)
{
    fake_display_t *display = context;

    display->resume_count++;
    return SYS_OK;
}

static const display_device_ops_t display_ops = {
    fake_display_init,
    fake_display_flush,
    fake_display_backlight,
    fake_display_suspend,
    fake_display_resume
};

static status_t fake_input_init(void *context, uint16_t width,
                                uint16_t height)
{
    return context != 0 && width != 0u && height != 0u
        ? SYS_OK : ERR_INVALID_ARG;
}

static status_t fake_input_read(void *context, input_sample_t *sample)
{
    fake_input_t *input = context;

    *sample = input->sample;
    return SYS_OK;
}

static status_t fake_input_suspend(void *context)
{
    fake_input_t *input = context;

    input->suspend_count++;
    return SYS_OK;
}

static status_t fake_input_resume(void *context)
{
    fake_input_t *input = context;

    input->resume_count++;
    return SYS_OK;
}

static const input_device_ops_t input_ops = {
    fake_input_init, fake_input_read, fake_input_suspend, fake_input_resume
};

static status_t fake_relay_init(void *context, int inactive_level)
{
    fake_relay_t *relay = context;

    relay->level = inactive_level;
    return SYS_OK;
}

static status_t fake_relay_write(void *context, int level)
{
    fake_relay_t *relay = context;

    relay->level = level;
    return SYS_OK;
}

static status_t fake_relay_read(void *context, int *level)
{
    fake_relay_t *relay = context;

    *level = relay->level;
    return SYS_OK;
}

static const relay_ops_t relay_ops = {
    fake_relay_init,
    fake_relay_write,
    fake_relay_read,
    0,
    0
};

static status_t fake_cli_init(void *context)
{
    return context != 0 ? SYS_OK : ERR_INVALID_ARG;
}

static status_t fake_cli_read(void *context, uint8_t *data, size_t capacity,
                              size_t *length, uint32_t timeout_ms)
{
    fake_cli_t *cli = context;
    size_t remaining;

    (void)timeout_ms;
    remaining = strlen(cli->input) - cli->input_offset;
    if (remaining == 0u) {
        *length = 0u;
        return ERR_TIMEOUT;
    }
    if (remaining > capacity) {
        remaining = capacity;
    }
    memcpy(data, cli->input + cli->input_offset, remaining);
    cli->input_offset += remaining;
    *length = remaining;
    return SYS_OK;
}

static status_t fake_cli_write(void *context, const uint8_t *data,
                               size_t length, uint32_t timeout_ms)
{
    fake_cli_t *cli = context;

    (void)timeout_ms;
    assert(cli->output_length + length < sizeof(cli->output));
    memcpy(cli->output + cli->output_length, data, length);
    cli->output_length += length;
    cli->output[cli->output_length] = '\0';
    return SYS_OK;
}

static const cli_transport_ops_t cli_ops = {
    fake_cli_init,
    fake_cli_read,
    fake_cli_write,
    0,
    0
};

static status_t fake_command_handler(void *context,
                                     const cli_command_t *command,
                                     char *response, size_t capacity)
{
    fake_cli_t *cli = context;

    cli->last_command = *command;
    cli->command_count++;
    (void)snprintf(response, capacity, "accepted");
    return SYS_OK;
}

static void test_config_transaction(void)
{
    gateway_runtime_config_t config = make_config();
    gateway_storage_config_request_t request;
    config_subsystem_t service;
    config_patch_t patch;
    relay_config_t relay_config = { 1u, RELAY_DEENERGIZED };
    fake_relay_t relay_context;
    relay_t relay;
    alarm_subsystem_t alarm;
    gateway_runtime_config_t active;
    config_health_t health;

    assert(relay_construct(&relay, &relay_ops, &relay_context,
                           &relay_config) == SYS_OK);
    assert(alarm_subsystem_construct(&alarm, &relay, &config) == SYS_OK);
    assert(alarm_subsystem_start(&alarm) == SYS_OK);
    assert(config_subsystem_construct(&service, &config) == SYS_OK);

    patch.point_id = GATEWAY_POINT_LOOP_CURRENT;
    patch.field = CONFIG_FIELD_HIGH_THRESHOLD;
    patch.value = 22000;
    assert(config_subsystem_prepare(&service, &patch, &request) == SYS_OK);
    assert(request.config.revision == 8u);
    assert(request.config.rules[0].high_threshold == 22000);
    assert(alarm_subsystem_reconfigure(&alarm, &request.config) == SYS_OK);
    assert(config_subsystem_commit(&service, &request) == SYS_OK);
    assert(config_subsystem_get(&service, &active) == SYS_OK);
    assert(active.revision == 8u);
    assert(active.rules[0].high_threshold == 22000);
    assert(config_subsystem_get_health(&service, &health) == SYS_OK);
    assert(health.committed_requests == 1u);
    assert(health.pending_requests == 0u);
}

static void test_config_reject_isolation(void)
{
    gateway_runtime_config_t config = make_config();
    config_subsystem_t service;
    gateway_storage_config_request_t first;
    gateway_storage_config_request_t second;
    config_patch_t patch = { GATEWAY_POINT_LOOP_CURRENT,
                             CONFIG_FIELD_HIGH_THRESHOLD, 23000 };
    assert(config_subsystem_construct(&service, &config) == SYS_OK);
    assert(config_subsystem_prepare(&service, &patch, &first) == SYS_OK);
    patch.field = CONFIG_FIELD_HYSTERESIS;
    patch.value = 300;
    assert(config_subsystem_prepare(&service, &patch, &second) == ERR_DEVICE_NOT_READY);
    assert(config_subsystem_reject(&service, first.request_id + 1u, ERR_IO) == ERR_INVALID_ARG);
    assert(service.health.pending_requests == 1u);
    assert(config_subsystem_reject(&service, first.request_id, ERR_IO) == SYS_OK);
    assert(config_subsystem_prepare(&service, &patch, &second) == SYS_OK);
    assert(second.config.rules[0].high_threshold == config.rules[0].high_threshold);
    assert(config_subsystem_commit(&service, &first) == ERR_INVALID_ARG);
    assert(config_subsystem_reject(&service, first.request_id, ERR_IO) == ERR_INVALID_ARG);
    assert(config_subsystem_commit(&service, &second) == SYS_OK);
    assert(service.active.rules[0].hysteresis == 300);
    assert(service.health.pending_requests == 0u);
}

static void test_active_alarm_blocks_reconfigure(void)
{
    gateway_runtime_config_t config = make_config();
    fake_relay_t relay_context;
    relay_config_t relay_config = { 1u, RELAY_DEENERGIZED };
    relay_t relay;
    alarm_subsystem_t alarm;
    gateway_measurement_t measurement;
    gateway_alarm_event_t events[ALARM_MAX_EVENTS_PER_MEASUREMENT];
    size_t count;

    assert(relay_construct(&relay, &relay_ops, &relay_context,
                           &relay_config) == SYS_OK);
    assert(alarm_subsystem_construct(&alarm, &relay, &config) == SYS_OK);
    assert(alarm_subsystem_start(&alarm) == SYS_OK);
    memset(&measurement, 0, sizeof(measurement));
    measurement.point_id = GATEWAY_POINT_LOOP_CURRENT;
    measurement.quality = GATEWAY_QUALITY_GOOD;
    measurement.engineering_value = 21000;
    assert(alarm_subsystem_process(&alarm, &measurement, events,
        ALARM_MAX_EVENTS_PER_MEASUREMENT, &count) == SYS_OK);
    assert(count == 1u);
    config.revision++;
    config.rules[0].high_threshold = 23000;
    assert(alarm_subsystem_reconfigure(&alarm, &config) ==
           ERR_DEVICE_NOT_READY);
}

static void test_parser(void)
{
    cli_command_t command;
    char config_line[] = "config set 1001 high 23000";
    char page_line[] = "ui page alarms";
    char bad_line[] = "config set 1001 high nope";
    char rtos_task_line[] = "rtos task";
    char rtos_runtime_line[] = "rtos runtime";
    char rtos_timing_line[] = "rtos timing";
    char power_lock_line[] = "power lock";
    char power_stop_line[] = "power stop 5000 CONFIRM";
    char power_standby_line[] = "power standby CONFIRM";
    char power_cancel_line[] = "power cancel";
    char bad_power_stop_line[] = "power stop 5000 confirm";
    char slot_line[] = "slot status";
    char fault_line[] = "fault show";
    char fault_clear_line[] = "fault clear";
    char fault_inject_line[] = "fault inject watchdog CONFIRM";
    char bad_fault_inject_line[] = "fault inject watchdog confirm";
    char ota_check_line[] = "ota check";
    char ota_start_line[] = "ota start";
    char ota_apply_line[] = "ota apply";
    char ota_cancel_line[] = "ota cancel";

    assert(cli_parse_command(config_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_CONFIG_SET);
    assert(command.argument.config_patch.point_id == 1001u);
    assert(command.argument.config_patch.field ==
           CONFIG_FIELD_HIGH_THRESHOLD);
    assert(command.argument.config_patch.value == 23000);
    assert(cli_parse_command(page_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_UI_PAGE);
    assert(command.argument.ui_page == UI_PAGE_ALARMS);
    assert(cli_parse_command(rtos_task_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_RTOS_TASK);
    assert(cli_parse_command(rtos_runtime_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_RTOS_RUNTIME);
    assert(cli_parse_command(rtos_timing_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_RTOS_TIMING);
    assert(cli_parse_command(power_lock_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_POWER_LOCK);
    assert(cli_parse_command(power_stop_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_POWER_STOP);
    assert(command.argument.power_duration_ms == 5000u);
    assert(cli_parse_command(power_standby_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_POWER_STANDBY);
    assert(cli_parse_command(power_cancel_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_POWER_CANCEL);
    assert(cli_parse_command(slot_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_SLOT_STATUS);
    assert(cli_parse_command(fault_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_FAULT_SHOW);
    assert(cli_parse_command(fault_clear_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_FAULT_CLEAR);
    assert(cli_parse_command(fault_inject_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_FAULT_INJECT);
    assert(command.argument.fault_injection == FAULT_INJECTION_WATCHDOG);
    assert(cli_parse_command(ota_check_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_OTA_CHECK);
    assert(cli_parse_command(ota_start_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_OTA_START);
    assert(cli_parse_command(ota_apply_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_OTA_APPLY);
    assert(cli_parse_command(ota_cancel_line, &command) == SYS_OK);
    assert(command.id == CLI_COMMAND_OTA_CANCEL);
    assert(cli_parse_command(bad_fault_inject_line, &command) ==
           ERR_INVALID_ARG);
    assert(cli_parse_command(bad_power_stop_line, &command) ==
           ERR_INVALID_ARG);
    assert(cli_parse_command(bad_line, &command) == ERR_INVALID_ARG);
}

static void test_cli_service(void)
{
    fake_cli_t fake;
    cli_transport_t transport;
    cli_subsystem_t cli;

    memset(&fake, 0, sizeof(fake));
    fake.input = "mqtt status\r\n";
    assert(cli_transport_construct(&transport, &cli_ops, &fake) == SYS_OK);
    assert(cli_subsystem_construct(&cli, &transport,
                                   fake_command_handler, &fake) == SYS_OK);
    assert(cli_subsystem_start(&cli) == SYS_OK);
    assert(cli_subsystem_process(&cli, 0u) == SYS_OK);
    assert(fake.command_count == 1u);
    assert(fake.last_command.id == CLI_COMMAND_MQTT_STATUS);
    assert(strstr(fake.output, "accepted") != 0);
}

static void test_ui_pages(void)
{
    fake_display_t fake;
    fake_display_t fallback_fake;
    fake_input_t fake_input;
    display_device_t display;
    display_device_t fallback_display;
    input_device_t input;
    ui_subsystem_t ui;
    ui_subsystem_t fallback_ui;
    gateway_system_snapshot_t snapshot;
    gateway_runtime_config_t config = make_config();
    ui_health_t health;
    uint16_t draw_buffer_1[480u];
    uint16_t draw_buffer_2[480u];

    memset(&fake, 0, sizeof(fake));
    memset(&fallback_fake, 0, sizeof(fallback_fake));
    memset(&fake_input, 0, sizeof(fake_input));
    memset(&snapshot, 0, sizeof(snapshot));
    fake_input.sample.x = 20;
    fake_input.sample.y = 30;
    fake_input.sample.state = INPUT_STATE_PRESSED;
    snapshot.sequence = 11u;
    snapshot.latest.point_id = GATEWAY_POINT_LOOP_CURRENT;
    snapshot.latest.engineering_value = 12345;
    snapshot.latest.quality = GATEWAY_QUALITY_GOOD;
    assert(display_device_construct(&display, &display_ops, &fake,
                                    480u, 272u) == SYS_OK);
    assert(input_device_construct(&input, &input_ops, &fake_input,
                                  480u, 272u) == SYS_OK);
    assert(ui_subsystem_construct(&ui, &display, &input, 0, 0) == SYS_OK);
    assert(ui_subsystem_configure_draw_buffers(
        &ui, draw_buffer_1, draw_buffer_2, 480u, 0u) == SYS_OK);
    assert(ui_subsystem_start(&ui) == SYS_OK);
    assert(fake.backlight == 80u);
    assert(ui_subsystem_process(&ui, 100u, &snapshot, &config) == SYS_OK);
    assert(ui_subsystem_take_input_activity(&ui) == 1u);
    assert(ui_subsystem_navigate(&ui, UI_PAGE_PARAMETERS) == SYS_OK);
    assert(ui_subsystem_process(&ui, 200u, &snapshot, &config) == SYS_OK);
    assert(ui_subsystem_get_health(&ui, &health) == SYS_OK);
    assert(health.active_page == UI_PAGE_PARAMETERS);
    assert(health.page_transitions == 2u);
    assert(health.refresh_count == 2u);
    assert(health.draw_buffer_count == 2u);
    assert(health.draw_buffer_degraded == 0u);
    assert(fake.flush_count != 0u);
    assert(ui_subsystem_set_power_state(&ui, UI_POWER_ECO) == SYS_OK);
    assert(fake.backlight == 20u);
    assert(ui_subsystem_set_power_state(&ui, UI_POWER_SUSPENDED) == SYS_OK);
    assert(fake.backlight == 0u);
    assert(fake.suspend_count == 1u && fake_input.suspend_count == 1u);
    assert(ui_subsystem_set_power_state(&ui, UI_POWER_ACTIVE) == SYS_OK);
    assert(fake.backlight == 80u);
    assert(fake.resume_count == 1u && fake_input.resume_count == 1u);

    assert(display_device_construct(&fallback_display, &display_ops,
                                    &fallback_fake, 480u, 272u) == SYS_OK);
    assert(ui_subsystem_construct(&fallback_ui, &fallback_display,
                                  0, 0, 0) == SYS_OK);
    assert(ui_subsystem_configure_draw_buffers(
        &fallback_ui, 0, 0, 0u, 1u) == SYS_OK);
    assert(ui_subsystem_get_health(&fallback_ui, &health) == SYS_OK);
    assert(health.draw_buffer_count == 1u);
    assert(health.draw_buffer_degraded == 1u);
}

int main(void)
{
    test_config_transaction();
    test_config_reject_isolation();
    test_active_alarm_blocks_reconfigure();
    test_parser();
    test_cli_service();
    test_ui_pages();
    puts("ui_cli_config: PASS");
    return 0;
}
