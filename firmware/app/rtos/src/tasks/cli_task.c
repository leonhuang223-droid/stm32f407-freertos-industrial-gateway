#include "runtime_internal.h"
#include <stdio.h>
#include <string.h>

static status_t handle_cli_command(void *opaque,
                                   const cli_command_t *command,
                                   char *response,
                                   size_t capacity);

void cli_task(void *argument)
{
    app_cli_task_context_t *context = argument;
    status_t startup_status;

    (void)cli_subsystem_set_command_handler(
        context->cli, handle_cli_command, context);
    startup_status = cli_subsystem_start(context->cli);
    (*context->cli_startup_status) = startup_status;
    if (startup_status == SYS_OK) {
        xEventGroupSetBits(channels.system_events, SYSTEM_EVENT_CLI_READY);
    } else {
        xEventGroupClearBits(channels.system_events, SYSTEM_EVENT_CLI_READY);
    }

    for (;;) {
        if (startup_status == SYS_OK) {
            (void)cli_subsystem_process(context->cli, 1000u);
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000u));
        }
        app_runtime_mark_alive(GATEWAY_TASK_CLI);
    }
}

/** Command handlers run in the CLI owner task.
 * @author 兆鸣嵌入式
 */
typedef struct {
    app_cli_task_context_t *context;
    const cli_command_t *command;
    char *response;
    size_t capacity;
    EventBits_t bits;
} cli_dispatch_t;

typedef status_t (*cli_dispatch_handler_t)(const cli_dispatch_t *request);

static status_t handle_help(const cli_dispatch_t *request)
{
    char *response = request->response;
    size_t capacity = request->capacity;
    status_t status = SYS_OK;

    (void)snprintf(
        response,
        capacity,
        "help | status | rtos [task|queue|runtime|timing] | sensor list\r\n"
        "alarm list|ack ID | fault show|clear | slot status\r\n"
        "mqtt status | storage status | config show\r\n"
        "config set POINT FIELD VALUE | ui page NAME\r\n"
        "ota status|check|start|apply|cancel | power status|stats|lock\r\n"
        "power stop MS CONFIRM | power standby CONFIRM | power cancel\r\n"
        "debug only: fault inject hardfault|watchdog CONFIRM");
    return status;
}

static status_t handle_status(const cli_dispatch_t *request)
{
    app_cli_task_context_t *context = request->context;
    char *response = request->response;
    size_t capacity = request->capacity;
    EventBits_t bits = request->bits;
    status_t status = SYS_OK;

    if (xSemaphoreTake(channels.snapshot_mutex, pdMS_TO_TICKS(20u)) == pdPASS) {
        gateway_system_snapshot_t snapshot = (*context->snapshot);
        xSemaphoreGive(channels.snapshot_mutex);
        (void)snprintf(response,
                       capacity,
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
    return status;
}

static status_t handle_rtos(const cli_dispatch_t *request)
{
    app_cli_task_context_t *context = request->context;
    char *response = request->response;
    size_t capacity = request->capacity;

    {
        supervisor_health_t health;
        uint32_t heartbeat[GATEWAY_TASK_COUNT];

        app_critical_enter();
        (void)supervisor_subsystem_get_health(context->supervisor, &health);
        memcpy(heartbeat, *context->heartbeat, sizeof(heartbeat));
        app_critical_exit();
        (void)snprintf(response,
                       capacity,
                       "healthy=%u stale=0x%08lX feeds=%lu faults=%lu "
                       "heartbeat sup=%lu acq=%lu hub=%lu modbus=%lu can=%lu "
                       "net=%lu ota=%lu store=%lu ui=%lu cli=%lu",
                       (unsigned int)health.healthy,
                       (unsigned long)health.stale_task_mask,
                       (unsigned long)health.watchdog_refreshes,
                       (unsigned long)health.latched_faults,
                       (unsigned long)heartbeat[GATEWAY_TASK_SUPERVISOR],
                       (unsigned long)heartbeat[GATEWAY_TASK_ACQUISITION],
                       (unsigned long)heartbeat[GATEWAY_TASK_DATA_HUB],
                       (unsigned long)heartbeat[GATEWAY_TASK_MODBUS],
                       (unsigned long)heartbeat[GATEWAY_TASK_CAN],
                       (unsigned long)heartbeat[GATEWAY_TASK_NETWORK],
                       (unsigned long)heartbeat[GATEWAY_TASK_OTA],
                       (unsigned long)heartbeat[GATEWAY_TASK_STORAGE],
                       (unsigned long)heartbeat[GATEWAY_TASK_UI],
                       (unsigned long)heartbeat[GATEWAY_TASK_CLI]);
    }
    return SYS_OK;
}

static status_t handle_rtos_task(const cli_dispatch_t *request)
{
    char *response = request->response;
    size_t capacity = request->capacity;
    app_rtos_diagnostics_t diagnostics;
    status_t status = app_runtime_read_diagnostics(&diagnostics);
    if (status != SYS_OK) {
        return status;
    }

    (void)snprintf(response,
                   capacity,
                   "stack_hwm words sup=%lu acq=%lu hub=%lu modbus=%lu can=%lu "
                   "net=%lu ota=%lu store=%lu ui=%lu cli=%lu samples=%lu",
                   (unsigned long)diagnostics.stack_high_water[0],
                   (unsigned long)diagnostics.stack_high_water[1],
                   (unsigned long)diagnostics.stack_high_water[2],
                   (unsigned long)diagnostics.stack_high_water[3],
                   (unsigned long)diagnostics.stack_high_water[4],
                   (unsigned long)diagnostics.stack_high_water[5],
                   (unsigned long)diagnostics.stack_high_water[6],
                   (unsigned long)diagnostics.stack_high_water[7],
                   (unsigned long)diagnostics.stack_high_water[8],
                   (unsigned long)diagnostics.stack_high_water[9],
                   (unsigned long)diagnostics.samples);
    return SYS_OK;
}

static status_t handle_rtos_queue(const cli_dispatch_t *request)
{
    char *response = request->response;
    size_t capacity = request->capacity;
    app_rtos_diagnostics_t diagnostics;
    status_t status = app_runtime_read_diagnostics(&diagnostics);
    if (status != SYS_OK) {
        return status;
    }

    (void)snprintf(response,
                   capacity,
                   "queue cur/high measure=%u/%u can=%u/%u ui=%u/%u "
                   "tele=%u/%u alarm=%u/%u netctl=%u/%u result=%u/%u "
                   "ota=%u/%u log=%u/%u salarm=%u/%u config=%u/%u uicmd=%u/%u "
                   "otanet=%u/%u otastore=%u/%u power=%u/%u",
                   diagnostics.queue_current[0],
                   diagnostics.queue_high_water[0],
                   diagnostics.queue_current[1],
                   diagnostics.queue_high_water[1],
                   diagnostics.queue_current[2],
                   diagnostics.queue_high_water[2],
                   diagnostics.queue_current[3],
                   diagnostics.queue_high_water[3],
                   diagnostics.queue_current[4],
                   diagnostics.queue_high_water[4],
                   diagnostics.queue_current[5],
                   diagnostics.queue_high_water[5],
                   diagnostics.queue_current[6],
                   diagnostics.queue_high_water[6],
                   diagnostics.queue_current[7],
                   diagnostics.queue_high_water[7],
                   diagnostics.queue_current[8],
                   diagnostics.queue_high_water[8],
                   diagnostics.queue_current[9],
                   diagnostics.queue_high_water[9],
                   diagnostics.queue_current[10],
                   diagnostics.queue_high_water[10],
                   diagnostics.queue_current[11],
                   diagnostics.queue_high_water[11],
                   diagnostics.queue_current[12],
                   diagnostics.queue_high_water[12],
                   diagnostics.queue_current[13],
                   diagnostics.queue_high_water[13],
                   diagnostics.queue_current[14],
                   diagnostics.queue_high_water[14]);
    return SYS_OK;
}

static status_t handle_rtos_runtime(const cli_dispatch_t *request)
{
    char *response = request->response;
    size_t capacity = request->capacity;
    app_rtos_diagnostics_t diagnostics;
    status_t status = app_runtime_read_diagnostics(&diagnostics);
    if (status != SYS_OK) {
        return status;
    }

    (void)snprintf(response,
                   capacity,
                   "cpu permille sup=%u acq=%u hub=%u modbus=%u can=%u net=%u "
                   "ota=%u store=%u ui=%u cli=%u idle=%u system=%u samples=%lu "
                   "errors=%lu",
                   diagnostics.cpu_permille[0],
                   diagnostics.cpu_permille[1],
                   diagnostics.cpu_permille[2],
                   diagnostics.cpu_permille[3],
                   diagnostics.cpu_permille[4],
                   diagnostics.cpu_permille[5],
                   diagnostics.cpu_permille[6],
                   diagnostics.cpu_permille[7],
                   diagnostics.cpu_permille[8],
                   diagnostics.cpu_permille[9],
                   diagnostics.idle_cpu_permille,
                   diagnostics.system_cpu_permille,
                   (unsigned long)diagnostics.runtime_samples,
                   (unsigned long)diagnostics.runtime_errors);
    return SYS_OK;
}

static status_t handle_rtos_timing(const cli_dispatch_t *request)
{
    app_cli_task_context_t *context = request->context;
    char *response = request->response;
    size_t capacity = request->capacity;
    status_t status = SYS_OK;

    {
        periodic_timing_stats_t timing;

        app_critical_enter();
        status =
            periodic_timing_monitor_get(context->acquisition_timing, &timing);
        app_critical_exit();
        if (status == SYS_OK) {
            (void)snprintf(
                response,
                capacity,
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
            (void)snprintf(
                response, capacity, "acq timing: %s", error_to_string(status));
        }
    }
    return status;
}

static status_t handle_sensor_list(const cli_dispatch_t *request)
{
    char *response = request->response;
    size_t capacity = request->capacity;

    (void)snprintf(response,
                   capacity,
                   "1001 ADS1115 4-20mA | 1002 MAX31865 PT100 | "
                   "1003/1004 SHT30 | 2001 Modbus | 3001 CAN");
    return SYS_OK;
}

static status_t handle_alarm_list(const cli_dispatch_t *request)
{
    app_cli_task_context_t *context = request->context;
    char *response = request->response;
    size_t capacity = request->capacity;
    status_t status = SYS_OK;

    if (xSemaphoreTake(channels.snapshot_mutex, pdMS_TO_TICKS(20u)) == pdPASS) {
        uint32_t active = context->snapshot->active_alarm_count;
        xSemaphoreGive(channels.snapshot_mutex);
        (void)snprintf(response, capacity, "active=%lu", (unsigned long)active);
    } else {
        status = ERR_TIMEOUT;
    }
    return status;
}

static status_t handle_alarm_ack(const cli_dispatch_t *request)
{
    const cli_command_t *command = request->command;
    char *response = request->response;
    size_t capacity = request->capacity;
    status_t status = SYS_OK;

    status = acknowledge_alarm(command->argument.alarm_event_id);
    (void)snprintf(response,
                   capacity,
                   "alarm ack %lu: %s",
                   (unsigned long)command->argument.alarm_event_id,
                   error_to_string(status));
    return status;
}

static status_t handle_mqtt_status(const cli_dispatch_t *request)
{
    char *response = request->response;
    size_t capacity = request->capacity;
    EventBits_t bits = request->bits;

    (void)snprintf(response,
                   capacity,
                   "network=%s mqtt=%s ota_lease=%s",
                   (bits & SYSTEM_EVENT_NETWORK_UP) != 0u ? "up" : "down",
                   (bits & SYSTEM_EVENT_MQTT_READY) != 0u ? "ready"
                                                          : "not-ready",
                   (bits & SYSTEM_EVENT_OTA_ACTIVE) != 0u ? "active" : "idle");
    return SYS_OK;
}

static status_t handle_storage_status(const cli_dispatch_t *request)
{
    app_cli_task_context_t *context = request->context;
    char *response = request->response;
    size_t capacity = request->capacity;
    EventBits_t bits = request->bits;

    {
        storage_health_t health;

        memset(&health, 0, sizeof(health));
        app_critical_enter();
        (void)storage_subsystem_get_health(context->storage, &health);
        app_critical_exit();
        (void)snprintf(
            response,
            capacity,
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
    return SYS_OK;
}

static status_t handle_config_show(const cli_dispatch_t *request)
{
    char *response = request->response;
    size_t capacity = request->capacity;
    status_t status = SYS_OK;

    {
        gateway_runtime_config_t config;
        config_health_t health;
        status = app_config_read(&config, &health, 50u);
        if (status != SYS_OK) {
            return status;
        }
        (void)snprintf(response,
                       capacity,
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
    }
    return status;
}

static status_t handle_config_set(const cli_dispatch_t *request)
{
    const cli_command_t *command = request->command;
    char *response = request->response;
    size_t capacity = request->capacity;
    status_t status = SYS_OK;

    {
        uint32_t request_id = 0u;
        status =
            submit_config_patch(&command->argument.config_patch, &request_id);
        (void)snprintf(response,
                       capacity,
                       "config request=%lu status=%s",
                       (unsigned long)request_id,
                       error_to_string(status));
    }
    return status;
}

static status_t handle_ui_page(const cli_dispatch_t *request)
{
    const cli_command_t *command = request->command;
    char *response = request->response;
    size_t capacity = request->capacity;
    status_t status = SYS_OK;

    status = app_rtos_submit_ui_page(command->argument.ui_page);
    (void)snprintf(response,
                   capacity,
                   "ui page=%s status=%s",
                   ui_page_name(command->argument.ui_page),
                   error_to_string(status));
    return status;
}

static status_t handle_ota_status(const cli_dispatch_t *request)
{
    app_cli_task_context_t *context = request->context;
    char *response = request->response;
    size_t capacity = request->capacity;
    EventBits_t bits = request->bits;

    uint8_t initialized;
    ota_status_t ota_status;
    status_t status;

    app_critical_enter();
    initialized = context->ota->initialized;
    status = ota_manager_get_status(context->ota, &ota_status);
    app_critical_exit();

    if (initialized != 0u) {
        if (status == SYS_OK) {
            (void)snprintf(response,
                           capacity,
                           "ota=%s lease=%s bytes=%lu/%lu chunks=%lu target=%u "
                           "crc=%u sha=%u last=%s",
                           ota_state_name(ota_status.state),
                           (bits & SYSTEM_EVENT_OTA_ACTIVE) != 0u ? "active"
                                                                  : "idle",
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
        (void)snprintf(response,
                       capacity,
                       "ota=idle lease=%s",
                       (bits & SYSTEM_EVENT_OTA_ACTIVE) != 0u ? "active"
                                                              : "idle");
    }
    return SYS_OK;
}

static status_t handle_ota_check(const cli_dispatch_t *request)
{
    app_cli_task_context_t *context = request->context;
    const cli_command_t *command = request->command;
    char *response = request->response;
    size_t capacity = request->capacity;
    status_t status = SYS_OK;

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
        ota.request_id = (*context->heartbeat)[GATEWAY_TASK_CLI];
        status = app_rtos_submit_ota_command(&ota);
        (void)snprintf(response,
                       capacity,
                       "ota request=%lu status=%s",
                       (unsigned long)ota.request_id,
                       error_to_string(status));
    }
    return status;
}

static status_t handle_power_status(const cli_dispatch_t *request)
{
    app_cli_task_context_t *context = request->context;
    char *response = request->response;
    size_t capacity = request->capacity;
    EventBits_t bits = request->bits;

    {
        deep_power_health_t deep_health;
        power_manager_t power;
        uint32_t watchdog_remaining;

        app_critical_enter();
        (void)deep_power_controller_get_health(context->deep_power,
                                               &deep_health);
        power = *context->power;
        watchdog_remaining = watchdog_device_remaining_ms(context->watchdog);
        app_critical_exit();
        (void)snprintf(
            response,
            capacity,
            "mode=%s policy=%s deepest=%s locks=0x%02lX leak=0x%02lX "
            "iwdg_remaining_ms=%lu deep_state=%u pending=%u "
            "stop_cap=%u standby_cap=%u ack=0x%02lX deep_last=%s last=%s",
            power_mode_name(power.current_mode),
            power_policy_name(power.policy),
            power_mode_name(power_manager_deepest_allowed(&power)),
            (unsigned long)power.stats.lock_mask,
            (unsigned long)power.stats.leak_mask,
            (unsigned long)watchdog_remaining,
            (unsigned int)deep_health.state,
            (unsigned int)deep_health.request_pending,
            (unsigned int)context->deep_power->config.stop_enabled,
            (unsigned int)context->deep_power->config.standby_enabled,
            (unsigned long)deep_power_quiesced_mask(bits),
            error_to_string(deep_health.last_error),
            error_to_string(power.last_error));
    }
    return SYS_OK;
}

static status_t handle_power_stats(const cli_dispatch_t *request)
{
    app_cli_task_context_t *context = request->context;
    char *response = request->response;
    size_t capacity = request->capacity;

    power_manager_stats_t power_stats;
    deep_power_health_t deep_health;
    critical_timing_monitor_t critical_timing;

    app_critical_enter();
    (void)power_manager_get_stats(context->power, &power_stats);
    (void)deep_power_controller_get_health(context->deep_power, &deep_health);
    critical_timing = *context->critical_timing;
    app_critical_exit();
    (void)snprintf(
        response,
        capacity,
        "acquire=%lu release=%lu release_err=%lu leak=%lu "
        "sleep attempt=%lu enter=%lu reject_short=%lu "
        "reject_lock=%lu reject_iwdg=%lu planned_ms=%lu max_plan_ms=%lu "
        "deep_req=%lu wait=%lu stop=%lu standby=%lu restore=%lu fail=%lu "
        "critical_max_cycles=%lu nesting_max=%u pair_err=%lu",
        (unsigned long)power_stats.acquire_count,
        (unsigned long)power_stats.release_count,
        (unsigned long)power_stats.release_errors,
        (unsigned long)power_stats.leak_events,
        (unsigned long)power_stats.sleep_attempts,
        (unsigned long)power_stats.sleep_entries,
        (unsigned long)power_stats.sleep_rejections[0],
        (unsigned long)power_stats.sleep_rejections[1],
        (unsigned long)power_stats.sleep_rejections[2],
        (unsigned long)power_stats.cumulative_sleep_budget_ms,
        (unsigned long)power_stats.longest_sleep_budget_ms,
        (unsigned long)deep_health.request_count,
        (unsigned long)deep_health.quiesce_waits,
        (unsigned long)deep_health.stop_entries,
        (unsigned long)deep_health.standby_entries,
        (unsigned long)deep_health.restore_count,
        (unsigned long)deep_health.failures,
        (unsigned long)critical_timing.longest_cycles,
        (unsigned int)critical_timing.maximum_nesting,
        (unsigned long)critical_timing.pairing_errors);
    return SYS_OK;
}

static status_t handle_power_lock(const cli_dispatch_t *request)
{
    app_cli_task_context_t *context = request->context;
    char *response = request->response;
    size_t capacity = request->capacity;

    uint16_t refcount[PM_LOCK_COUNT];
    uint32_t mask;

    app_critical_enter();
    memcpy(refcount, context->power->lock_refcount, sizeof(refcount));
    mask = context->power->stats.lock_mask;
    app_critical_exit();
    (void)snprintf(response,
                   capacity,
                   "mask=0x%02lX ota=%u flash=%u net=%u modbus=%u can=%u "
                   "ui=%u alarm=%u",
                   (unsigned long)mask,
                   refcount[PM_LOCK_OTA],
                   refcount[PM_LOCK_FLASH_WRITE],
                   refcount[PM_LOCK_NETWORK_TX],
                   refcount[PM_LOCK_MODBUS_TRANSACTION],
                   refcount[PM_LOCK_CAN_MONITORING],
                   refcount[PM_LOCK_UI_ACTIVE],
                   refcount[PM_LOCK_ALARM_ACTIVE]);
    return SYS_OK;
}

static status_t handle_power_stop(const cli_dispatch_t *request)
{
    app_cli_task_context_t *context = request->context;
    const cli_command_t *command = request->command;
    char *response = request->response;
    size_t capacity = request->capacity;
    status_t status = SYS_OK;

    {
        power_command_t power_command = {POWER_COMMAND_REQUEST_STOP,
                                         command->argument.power_duration_ms};

        if (context->deep_power->config.stop_enabled == 0u) {
            status = ERR_UNSUPPORTED;
        } else if (power_command.duration_ms <
                       context->deep_power->config.minimum_stop_ms ||
                   power_command.duration_ms >
                       context->deep_power->config.maximum_stop_ms) {
            status = ERR_INVALID_ARG;
        } else {
            status =
                xQueueSend(channels.power_command, &power_command, 0u) == pdPASS
                    ? SYS_OK
                    : ERR_QUEUE_FULL;
        }
        (void)snprintf(response,
                       capacity,
                       "power stop queued_ms=%lu status=%s",
                       (unsigned long)command->argument.power_duration_ms,
                       error_to_string(status));
    }
    return status;
}

static status_t handle_power_standby(const cli_dispatch_t *request)
{
    app_cli_task_context_t *context = request->context;
    char *response = request->response;
    size_t capacity = request->capacity;
    status_t status = SYS_OK;

    {
        power_command_t power_command = {POWER_COMMAND_REQUEST_STANDBY, 0u};

        status =
            context->deep_power->config.standby_enabled == 0u
                ? ERR_UNSUPPORTED
                : (xQueueSend(channels.power_command, &power_command, 0u) ==
                           pdPASS
                       ? SYS_OK
                       : ERR_QUEUE_FULL);
        (void)snprintf(response,
                       capacity,
                       "power standby queued status=%s",
                       error_to_string(status));
    }
    return status;
}

static status_t handle_power_cancel(const cli_dispatch_t *request)
{
    char *response = request->response;
    size_t capacity = request->capacity;
    status_t status = SYS_OK;

    {
        power_command_t power_command = {POWER_COMMAND_CANCEL, 0u};

        status =
            xQueueSend(channels.power_command, &power_command, 0u) == pdPASS
                ? SYS_OK
                : ERR_QUEUE_FULL;
        (void)snprintf(response,
                       capacity,
                       "power cancel queued status=%s",
                       error_to_string(status));
    }
    return status;
}

static status_t handle_slot_status(const cli_dispatch_t *request)
{
    app_cli_task_context_t *context = request->context;
    char *response = request->response;
    size_t capacity = request->capacity;

    boot_confirmation_health_t health;

    app_critical_enter();
    (void)boot_confirmation_get_health(context->boot_confirmation, &health);
    app_critical_exit();
    (void)snprintf(response,
                   capacity,
                   "running=%u active=%u pending=%u state=%u confirmed=%u "
                   "attempts=%lu commits=%lu seq=%lu last=%s",
                   (unsigned int)health.running_slot,
                   (unsigned int)health.active_slot,
                   (unsigned int)health.pending_slot,
                   (unsigned int)health.boot_state,
                   (unsigned int)health.confirmed,
                   (unsigned long)health.attempts,
                   (unsigned long)health.commits,
                   (unsigned long)health.metadata_sequence,
                   error_to_string(health.last_error));
    return SYS_OK;
}

static status_t handle_fault_show(const cli_dispatch_t *request)
{
    app_cli_task_context_t *context = request->context;
    char *response = request->response;
    size_t capacity = request->capacity;
    EventBits_t bits = request->bits;

    {
        fault_record_t record;
        storage_health_t storage_health;
        supervisor_health_t supervisor_health;
        const char *source = "rtc";
        status_t record_status =
            fault_recorder_get(context->fault_recorder, &record);

        memset(&storage_health, 0, sizeof(storage_health));
        app_critical_enter();
        (void)supervisor_subsystem_get_health(context->supervisor,
                                              &supervisor_health);
        (void)storage_subsystem_get_health(context->storage, &storage_health);
        if (record_status != SYS_OK) {
            record_status =
                storage_subsystem_load_latest_fault(context->storage, &record);
            source = "w25";
        }
        app_critical_exit();
        if (record_status == SYS_OK) {
            (void)snprintf(response,
                           capacity,
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
                           (unsigned long)supervisor_health.stale_task_mask,
                           (unsigned long)supervisor_health.latched_faults,
                           error_to_string(supervisor_health.last_error));
        } else {
            (void)snprintf(
                response,
                capacity,
                "source=none archive=%u/%lu | event=%u "
                "stale=0x%08lX latched=%lu "
                "supervisor=%s drop measure=%lu can=%lu slog=%lu "
                "salarm=%lu config=%lu nalarm=%lu nctl=%lu",
                (unsigned int)storage_health.latest_crash_valid,
                (unsigned long)storage_health.latest_crash_sequence,
                (bits & SYSTEM_EVENT_FAULT_ACTIVE) != 0u ? 1u : 0u,
                (unsigned long)supervisor_health.stale_task_mask,
                (unsigned long)supervisor_health.latched_faults,
                error_to_string(supervisor_health.last_error),
                (unsigned long)(*context->measurement_publish_drops),
                (unsigned long)(*context->can_tx_publish_drops),
                (unsigned long)(*context->storage_log_publish_drops),
                (unsigned long)(*context->storage_alarm_publish_drops),
                (unsigned long)(*context->storage_config_publish_drops),
                (unsigned long)(*context->network_alarm_publish_drops),
                (unsigned long)(*context->network_control_publish_drops));
        }
    }
    return SYS_OK;
}

static status_t handle_fault_clear(const cli_dispatch_t *request)
{
    app_cli_task_context_t *context = request->context;
    char *response = request->response;
    size_t capacity = request->capacity;
    status_t status = SYS_OK;

    status = fault_recorder_clear(context->fault_recorder);
    (void)snprintf(
        response, capacity, "fault cookie clear: %s", error_to_string(status));
    return status;
}

static status_t handle_fault_inject(const cli_dispatch_t *request)
{
    app_cli_task_context_t *context = request->context;
    const cli_command_t *command = request->command;
    char *response = request->response;
    size_t capacity = request->capacity;
    status_t status = SYS_OK;

    status = fault_recorder_inject(context->fault_recorder,
                                   command->argument.fault_injection,
                                   FAULT_INJECTION_CONFIRMATION);
    (void)snprintf(response,
                   capacity,
                   "fault injection=%u status=%s",
                   (unsigned int)command->argument.fault_injection,
                   error_to_string(status));
    return status;
}

static status_t handle_unsupported(const cli_dispatch_t *request)
{
    char *response = request->response;
    size_t capacity = request->capacity;
    status_t status = SYS_OK;

    status = ERR_UNSUPPORTED;
    (void)snprintf(response, capacity, "%s", error_to_string(status));
    return status;
}
static const struct {
    cli_command_id_t command;
    cli_dispatch_handler_t handler;
} command_handlers[] = {
    {CLI_COMMAND_HELP, handle_help},
    {CLI_COMMAND_STATUS, handle_status},
    {CLI_COMMAND_RTOS, handle_rtos},
    {CLI_COMMAND_RTOS_TASK, handle_rtos_task},
    {CLI_COMMAND_RTOS_QUEUE, handle_rtos_queue},
    {CLI_COMMAND_RTOS_RUNTIME, handle_rtos_runtime},
    {CLI_COMMAND_RTOS_TIMING, handle_rtos_timing},
    {CLI_COMMAND_SENSOR_LIST, handle_sensor_list},
    {CLI_COMMAND_ALARM_LIST, handle_alarm_list},
    {CLI_COMMAND_ALARM_ACK, handle_alarm_ack},
    {CLI_COMMAND_MQTT_STATUS, handle_mqtt_status},
    {CLI_COMMAND_STORAGE_STATUS, handle_storage_status},
    {CLI_COMMAND_CONFIG_SHOW, handle_config_show},
    {CLI_COMMAND_CONFIG_SET, handle_config_set},
    {CLI_COMMAND_UI_PAGE, handle_ui_page},
    {CLI_COMMAND_OTA_STATUS, handle_ota_status},
    {CLI_COMMAND_OTA_CHECK, handle_ota_check},
    {CLI_COMMAND_OTA_START, handle_ota_check},
    {CLI_COMMAND_OTA_APPLY, handle_ota_check},
    {CLI_COMMAND_OTA_CANCEL, handle_ota_check},
    {CLI_COMMAND_POWER_STATUS, handle_power_status},
    {CLI_COMMAND_POWER_STATS, handle_power_stats},
    {CLI_COMMAND_POWER_LOCK, handle_power_lock},
    {CLI_COMMAND_POWER_STOP, handle_power_stop},
    {CLI_COMMAND_POWER_STANDBY, handle_power_standby},
    {CLI_COMMAND_POWER_CANCEL, handle_power_cancel},
    {CLI_COMMAND_SLOT_STATUS, handle_slot_status},
    {CLI_COMMAND_FAULT_SHOW, handle_fault_show},
    {CLI_COMMAND_FAULT_CLEAR, handle_fault_clear},
    {CLI_COMMAND_FAULT_INJECT, handle_fault_inject},
};

static status_t handle_cli_command(void *opaque,
                                   const cli_command_t *command,
                                   char *response,
                                   size_t capacity)
{
    cli_dispatch_t request;
    size_t i;

    if (opaque == 0 || command == 0 || response == 0 || capacity == 0u) {
        return ERR_INVALID_ARG;
    }
    request.context = opaque;
    request.command = command;
    request.response = response;
    request.capacity = capacity;
    request.bits = xEventGroupGetBits(channels.system_events);
    for (i = 0u; i < sizeof(command_handlers) / sizeof(command_handlers[0]);
         ++i) {
        if (command_handlers[i].command == command->id) {
            return command_handlers[i].handler(&request);
        }
    }
    return handle_unsupported(&request);
}
