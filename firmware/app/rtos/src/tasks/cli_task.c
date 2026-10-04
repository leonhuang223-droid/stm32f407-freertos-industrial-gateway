#include "runtime_internal.h"
#include <stdio.h>
#include <string.h>

static status_t handle_cli_command(void *opaque,
                                   const cli_command_t *command,
                                   char *response, size_t capacity);

void cli_task(void *argument)
{
    app_cli_task_context_t *context = argument;
    status_t startup_status;

    (void)cli_subsystem_set_command_handler(context->cli,
                                            handle_cli_command, context);
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

static status_t handle_cli_command(void *opaque,
                                   const cli_command_t *command,
                                   char *response, size_t capacity)
{
    app_cli_task_context_t *context = opaque;
    EventBits_t bits;
    status_t status = SYS_OK;

    if (context == 0 || command == 0 || response == 0 || capacity == 0u) {
        return ERR_INVALID_ARG;
    }
    bits = xEventGroupGetBits(channels.system_events);
    switch (command->id) {
    case CLI_COMMAND_HELP:
        (void)snprintf(response, capacity,
            "help | status | rtos [task|queue|runtime|timing] | sensor list\r\n"
            "alarm list|ack ID | fault show|clear | slot status\r\n"
            "mqtt status | storage status | config show\r\n"
            "config set POINT FIELD VALUE | ui page NAME\r\n"
            "ota status|check|start|apply|cancel | power status|stats|lock\r\n"
            "power stop MS CONFIRM | power standby CONFIRM | power cancel\r\n"
            "debug only: fault inject hardfault|watchdog CONFIRM");
        break;
    case CLI_COMMAND_STATUS:
        if (xSemaphoreTake(channels.snapshot_mutex,
                           pdMS_TO_TICKS(20u)) == pdPASS) {
            gateway_system_snapshot_t snapshot = (*context->snapshot);
            xSemaphoreGive(channels.snapshot_mutex);
            (void)snprintf(response, capacity,
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
        break;
    case CLI_COMMAND_RTOS:
        {
            supervisor_health_t health;

            (void)supervisor_subsystem_get_health(context->supervisor,
                                                  &health);
            (void)snprintf(response, capacity,
            "healthy=%u stale=0x%08lX feeds=%lu faults=%lu "
            "heartbeat sup=%lu acq=%lu hub=%lu modbus=%lu can=%lu "
            "net=%lu ota=%lu store=%lu ui=%lu cli=%lu",
            (unsigned int)health.healthy,
            (unsigned long)health.stale_task_mask,
            (unsigned long)health.watchdog_refreshes,
            (unsigned long)health.latched_faults,
            (unsigned long)(*context->heartbeat)[GATEWAY_TASK_SUPERVISOR],
            (unsigned long)(*context->heartbeat)[GATEWAY_TASK_ACQUISITION],
            (unsigned long)(*context->heartbeat)[GATEWAY_TASK_DATA_HUB],
            (unsigned long)(*context->heartbeat)[GATEWAY_TASK_MODBUS],
            (unsigned long)(*context->heartbeat)[GATEWAY_TASK_CAN],
            (unsigned long)(*context->heartbeat)[GATEWAY_TASK_NETWORK],
            (unsigned long)(*context->heartbeat)[GATEWAY_TASK_OTA],
            (unsigned long)(*context->heartbeat)[GATEWAY_TASK_STORAGE],
            (unsigned long)(*context->heartbeat)[GATEWAY_TASK_UI],
            (unsigned long)(*context->heartbeat)[GATEWAY_TASK_CLI]);
        }
        break;
    case CLI_COMMAND_RTOS_TASK:
        (void)snprintf(response, capacity,
            "stack_hwm words sup=%lu acq=%lu hub=%lu modbus=%lu can=%lu "
            "net=%lu ota=%lu store=%lu ui=%lu cli=%lu samples=%lu",
            (unsigned long)context->rtos_diagnostics->stack_high_water[0],
            (unsigned long)context->rtos_diagnostics->stack_high_water[1],
            (unsigned long)context->rtos_diagnostics->stack_high_water[2],
            (unsigned long)context->rtos_diagnostics->stack_high_water[3],
            (unsigned long)context->rtos_diagnostics->stack_high_water[4],
            (unsigned long)context->rtos_diagnostics->stack_high_water[5],
            (unsigned long)context->rtos_diagnostics->stack_high_water[6],
            (unsigned long)context->rtos_diagnostics->stack_high_water[7],
            (unsigned long)context->rtos_diagnostics->stack_high_water[8],
            (unsigned long)context->rtos_diagnostics->stack_high_water[9],
            (unsigned long)context->rtos_diagnostics->samples);
        break;
    case CLI_COMMAND_RTOS_QUEUE:
        (void)snprintf(response, capacity,
            "queue cur/high measure=%u/%u can=%u/%u ui=%u/%u "
            "tele=%u/%u alarm=%u/%u netctl=%u/%u result=%u/%u "
            "ota=%u/%u log=%u/%u salarm=%u/%u config=%u/%u uicmd=%u/%u "
            "otanet=%u/%u otastore=%u/%u power=%u/%u",
            context->rtos_diagnostics->queue_current[0],
            context->rtos_diagnostics->queue_high_water[0],
            context->rtos_diagnostics->queue_current[1],
            context->rtos_diagnostics->queue_high_water[1],
            context->rtos_diagnostics->queue_current[2],
            context->rtos_diagnostics->queue_high_water[2],
            context->rtos_diagnostics->queue_current[3],
            context->rtos_diagnostics->queue_high_water[3],
            context->rtos_diagnostics->queue_current[4],
            context->rtos_diagnostics->queue_high_water[4],
            context->rtos_diagnostics->queue_current[5],
            context->rtos_diagnostics->queue_high_water[5],
            context->rtos_diagnostics->queue_current[6],
            context->rtos_diagnostics->queue_high_water[6],
            context->rtos_diagnostics->queue_current[7],
            context->rtos_diagnostics->queue_high_water[7],
            context->rtos_diagnostics->queue_current[8],
            context->rtos_diagnostics->queue_high_water[8],
            context->rtos_diagnostics->queue_current[9],
            context->rtos_diagnostics->queue_high_water[9],
            context->rtos_diagnostics->queue_current[10],
            context->rtos_diagnostics->queue_high_water[10],
            context->rtos_diagnostics->queue_current[11],
            context->rtos_diagnostics->queue_high_water[11],
            context->rtos_diagnostics->queue_current[12],
            context->rtos_diagnostics->queue_high_water[12],
            context->rtos_diagnostics->queue_current[13],
            context->rtos_diagnostics->queue_high_water[13],
            context->rtos_diagnostics->queue_current[14],
            context->rtos_diagnostics->queue_high_water[14]);
        break;
    case CLI_COMMAND_RTOS_RUNTIME:
        (void)snprintf(response, capacity,
            "cpu permille sup=%u acq=%u hub=%u modbus=%u can=%u net=%u "
            "ota=%u store=%u ui=%u cli=%u idle=%u system=%u samples=%lu "
            "errors=%lu",
            context->rtos_diagnostics->cpu_permille[0],
            context->rtos_diagnostics->cpu_permille[1],
            context->rtos_diagnostics->cpu_permille[2],
            context->rtos_diagnostics->cpu_permille[3],
            context->rtos_diagnostics->cpu_permille[4],
            context->rtos_diagnostics->cpu_permille[5],
            context->rtos_diagnostics->cpu_permille[6],
            context->rtos_diagnostics->cpu_permille[7],
            context->rtos_diagnostics->cpu_permille[8],
            context->rtos_diagnostics->cpu_permille[9],
            context->rtos_diagnostics->idle_cpu_permille,
            context->rtos_diagnostics->system_cpu_permille,
            (unsigned long)context->rtos_diagnostics->runtime_samples,
            (unsigned long)context->rtos_diagnostics->runtime_errors);
        break;
    case CLI_COMMAND_RTOS_TIMING:
        {
            periodic_timing_stats_t timing;

            app_critical_enter();
            status = periodic_timing_monitor_get(
                context->acquisition_timing, &timing);
            app_critical_exit();
            if (status == SYS_OK) {
                (void)snprintf(response, capacity,
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
                (void)snprintf(response, capacity,
                               "acq timing: %s", error_to_string(status));
            }
        }
        break;
    case CLI_COMMAND_SENSOR_LIST:
        (void)snprintf(response, capacity,
            "1001 ADS1115 4-20mA | 1002 MAX31865 PT100 | "
            "1003/1004 SHT30 | 2001 Modbus | 3001 CAN");
        break;
    case CLI_COMMAND_ALARM_LIST:
        if (xSemaphoreTake(channels.snapshot_mutex,
                           pdMS_TO_TICKS(20u)) == pdPASS) {
            uint32_t active = context->snapshot->active_alarm_count;
            xSemaphoreGive(channels.snapshot_mutex);
            (void)snprintf(response, capacity, "active=%lu",
                (unsigned long)active);
        } else {
            status = ERR_TIMEOUT;
        }
        break;
    case CLI_COMMAND_ALARM_ACK:
        status = acknowledge_alarm(command->argument.alarm_event_id);
        (void)snprintf(response, capacity, "alarm ack %lu: %s",
            (unsigned long)command->argument.alarm_event_id,
            error_to_string(status));
        break;
    case CLI_COMMAND_MQTT_STATUS:
        (void)snprintf(response, capacity, "network=%s mqtt=%s ota_lease=%s",
            (bits & SYSTEM_EVENT_NETWORK_UP) != 0u ? "up" : "down",
            (bits & SYSTEM_EVENT_MQTT_READY) != 0u ? "ready" : "not-ready",
            (bits & SYSTEM_EVENT_OTA_ACTIVE) != 0u ? "active" : "idle");
        break;
    case CLI_COMMAND_STORAGE_STATUS:
        {
            storage_health_t health;

            memset(&health, 0, sizeof(health));
            app_critical_enter();
            (void)storage_subsystem_get_health(context->storage, &health);
            app_critical_exit();
            (void)snprintf(response, capacity,
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
        break;
    case CLI_COMMAND_CONFIG_SHOW:
        {
            gateway_runtime_config_t config;
            config_health_t health;
            status = app_config_read(&config, &health, 50u);
            if (status != SYS_OK) {
                break;
            }
            (void)snprintf(response, capacity,
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
        break;
    case CLI_COMMAND_CONFIG_SET:
        {
            uint32_t request_id = 0u;
            status = submit_config_patch(&command->argument.config_patch, &request_id);
            (void)snprintf(response, capacity,
                "config request=%lu status=%s",
                (unsigned long)request_id, error_to_string(status));
        }
        break;
    case CLI_COMMAND_UI_PAGE:
        status = app_rtos_submit_ui_page(command->argument.ui_page);
        (void)snprintf(response, capacity, "ui page=%s status=%s",
            ui_page_name(command->argument.ui_page),
            error_to_string(status));
        break;
    case CLI_COMMAND_OTA_STATUS:
        if (context->ota->initialized != 0u) {
            ota_status_t ota_status;

            if (ota_manager_get_status(context->ota, &ota_status) ==
                SYS_OK) {
                (void)snprintf(response, capacity,
                    "ota=%s lease=%s bytes=%lu/%lu chunks=%lu target=%u "
                    "crc=%u sha=%u last=%s",
                    ota_state_name(ota_status.state),
                    (bits & SYSTEM_EVENT_OTA_ACTIVE) != 0u
                        ? "active" : "idle",
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
            (void)snprintf(response, capacity, "ota=idle lease=%s",
                (bits & SYSTEM_EVENT_OTA_ACTIVE) != 0u
                    ? "active" : "idle");
        }
        break;
    case CLI_COMMAND_OTA_CHECK:
    case CLI_COMMAND_OTA_START:
    case CLI_COMMAND_OTA_APPLY:
    case CLI_COMMAND_OTA_CANCEL:
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
            (void)snprintf(response, capacity, "ota request=%lu status=%s",
                (unsigned long)ota.request_id, error_to_string(status));
        }
        break;
    case CLI_COMMAND_POWER_STATUS:
        {
        deep_power_health_t deep_health;

        app_critical_enter();
        (void)deep_power_controller_get_health(context->deep_power,
                                               &deep_health);
        app_critical_exit();
        (void)snprintf(response, capacity,
            "mode=%s policy=%s deepest=%s locks=0x%02lX leak=0x%02lX "
            "iwdg_remaining_ms=%lu deep_state=%u pending=%u "
            "stop_cap=%u standby_cap=%u ack=0x%02lX deep_last=%s last=%s",
            power_mode_name(context->power->current_mode),
            power_policy_name(context->power->policy),
            power_mode_name(power_manager_deepest_allowed(context->power)),
            (unsigned long)context->power->stats.lock_mask,
            (unsigned long)context->power->stats.leak_mask,
            (unsigned long)watchdog_device_remaining_ms(context->watchdog),
            (unsigned int)deep_health.state,
            (unsigned int)deep_health.request_pending,
            (unsigned int)context->deep_power->config.stop_enabled,
            (unsigned int)context->deep_power->config.standby_enabled,
            (unsigned long)deep_power_quiesced_mask(bits),
            error_to_string(deep_health.last_error),
            error_to_string(context->power->last_error));
        }
        break;
    case CLI_COMMAND_POWER_STATS:
        (void)snprintf(response, capacity,
            "acquire=%lu release=%lu release_err=%lu leak=%lu "
            "sleep attempt=%lu enter=%lu reject_short=%lu "
            "reject_lock=%lu reject_iwdg=%lu planned_ms=%lu max_plan_ms=%lu "
            "deep_req=%lu wait=%lu stop=%lu standby=%lu restore=%lu fail=%lu "
            "critical_max_cycles=%lu nesting_max=%u pair_err=%lu",
            (unsigned long)context->power->stats.acquire_count,
            (unsigned long)context->power->stats.release_count,
            (unsigned long)context->power->stats.release_errors,
            (unsigned long)context->power->stats.leak_events,
            (unsigned long)context->power->stats.sleep_attempts,
            (unsigned long)context->power->stats.sleep_entries,
            (unsigned long)context->power->stats.sleep_rejections[0],
            (unsigned long)context->power->stats.sleep_rejections[1],
            (unsigned long)context->power->stats.sleep_rejections[2],
            (unsigned long)context->power->stats.cumulative_sleep_budget_ms,
            (unsigned long)context->power->stats.longest_sleep_budget_ms,
            (unsigned long)context->deep_power->health.request_count,
            (unsigned long)context->deep_power->health.quiesce_waits,
            (unsigned long)context->deep_power->health.stop_entries,
            (unsigned long)context->deep_power->health.standby_entries,
            (unsigned long)context->deep_power->health.restore_count,
            (unsigned long)context->deep_power->health.failures,
            (unsigned long)context->critical_timing->longest_cycles,
            (unsigned int)context->critical_timing->maximum_nesting,
            (unsigned long)context->critical_timing->pairing_errors);
        break;
    case CLI_COMMAND_POWER_LOCK:
        (void)snprintf(response, capacity,
            "mask=0x%02lX ota=%u flash=%u net=%u modbus=%u can=%u "
            "ui=%u alarm=%u",
            (unsigned long)context->power->stats.lock_mask,
            context->power->lock_refcount[PM_LOCK_OTA],
            context->power->lock_refcount[PM_LOCK_FLASH_WRITE],
            context->power->lock_refcount[PM_LOCK_NETWORK_TX],
            context->power->lock_refcount[PM_LOCK_MODBUS_TRANSACTION],
            context->power->lock_refcount[PM_LOCK_CAN_MONITORING],
            context->power->lock_refcount[PM_LOCK_UI_ACTIVE],
            context->power->lock_refcount[PM_LOCK_ALARM_ACTIVE]);
        break;
    case CLI_COMMAND_POWER_STOP:
        {
        power_command_t request = {
            POWER_COMMAND_REQUEST_STOP,
            command->argument.power_duration_ms
        };

        if (context->deep_power->config.stop_enabled == 0u) {
            status = ERR_UNSUPPORTED;
        } else if (request.duration_ms <
                       context->deep_power->config.minimum_stop_ms ||
                   request.duration_ms >
                       context->deep_power->config.maximum_stop_ms) {
            status = ERR_INVALID_ARG;
        } else {
            status = xQueueSend(channels.power_command, &request, 0u) ==
                         pdPASS ? SYS_OK : ERR_QUEUE_FULL;
        }
        (void)snprintf(response, capacity,
            "power stop queued_ms=%lu status=%s",
            (unsigned long)command->argument.power_duration_ms,
            error_to_string(status));
        }
        break;
    case CLI_COMMAND_POWER_STANDBY:
        {
        power_command_t request = {
            POWER_COMMAND_REQUEST_STANDBY,
            0u
        };

        status = context->deep_power->config.standby_enabled == 0u
            ? ERR_UNSUPPORTED
            : (xQueueSend(channels.power_command, &request, 0u) == pdPASS
                ? SYS_OK : ERR_QUEUE_FULL);
        (void)snprintf(response, capacity,
            "power standby queued status=%s", error_to_string(status));
        }
        break;
    case CLI_COMMAND_POWER_CANCEL:
        {
        power_command_t request = { POWER_COMMAND_CANCEL, 0u };

        status = xQueueSend(channels.power_command, &request, 0u) == pdPASS
            ? SYS_OK : ERR_QUEUE_FULL;
        (void)snprintf(response, capacity,
            "power cancel queued status=%s", error_to_string(status));
        }
        break;
    case CLI_COMMAND_SLOT_STATUS:
        (void)snprintf(response, capacity,
            "running=%u active=%u pending=%u state=%u confirmed=%u "
            "attempts=%lu commits=%lu seq=%lu last=%s",
            (unsigned int)context->boot_confirmation->health.running_slot,
            (unsigned int)context->boot_confirmation->health.active_slot,
            (unsigned int)context->boot_confirmation->health.pending_slot,
            (unsigned int)context->boot_confirmation->health.boot_state,
            (unsigned int)context->boot_confirmation->health.confirmed,
            (unsigned long)context->boot_confirmation->health.attempts,
            (unsigned long)context->boot_confirmation->health.commits,
            (unsigned long)context->boot_confirmation->health.metadata_sequence,
            error_to_string(context->boot_confirmation->health.last_error));
        break;
    case CLI_COMMAND_FAULT_SHOW:
        {
            fault_record_t record;
            storage_health_t storage_health;
            const char *source = "rtc";
            status_t record_status = fault_recorder_get(
                context->fault_recorder, &record);

            memset(&storage_health, 0, sizeof(storage_health));
            app_critical_enter();
            (void)storage_subsystem_get_health(context->storage,
                                               &storage_health);
            if (record_status != SYS_OK) {
                record_status = storage_subsystem_load_latest_fault(
                    context->storage, &record);
                source = "w25";
            }
            app_critical_exit();
            if (record_status == SYS_OK) {
                (void)snprintf(response, capacity,
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
                    (unsigned long)context->supervisor->health.stale_task_mask,
                    (unsigned long)context->supervisor->health.latched_faults,
                    error_to_string(context->supervisor->health.last_error));
            } else {
                (void)snprintf(response, capacity,
                    "source=none archive=%u/%lu | event=%u "
                    "stale=0x%08lX latched=%lu "
                    "supervisor=%s drop measure=%lu can=%lu slog=%lu "
                    "salarm=%lu config=%lu nalarm=%lu nctl=%lu",
                    (unsigned int)storage_health.latest_crash_valid,
                    (unsigned long)storage_health.latest_crash_sequence,
                    (bits & SYSTEM_EVENT_FAULT_ACTIVE) != 0u ? 1u : 0u,
                    (unsigned long)context->supervisor->health.stale_task_mask,
                    (unsigned long)context->supervisor->health.latched_faults,
                    error_to_string(context->supervisor->health.last_error),
                    (unsigned long)(*context->measurement_publish_drops),
                    (unsigned long)(*context->can_tx_publish_drops),
                    (unsigned long)(*context->storage_log_publish_drops),
                    (unsigned long)(*context->storage_alarm_publish_drops),
                    (unsigned long)(*context->storage_config_publish_drops),
                    (unsigned long)(*context->network_alarm_publish_drops),
                    (unsigned long)(*context->network_control_publish_drops));
            }
        }
        break;
    case CLI_COMMAND_FAULT_CLEAR:
        status = fault_recorder_clear(context->fault_recorder);
        (void)snprintf(response, capacity, "fault cookie clear: %s",
                       error_to_string(status));
        break;
    case CLI_COMMAND_FAULT_INJECT:
        status = fault_recorder_inject(
            context->fault_recorder,
            command->argument.fault_injection,
            FAULT_INJECTION_CONFIRMATION);
        (void)snprintf(response, capacity, "fault injection=%u status=%s",
            (unsigned int)command->argument.fault_injection,
            error_to_string(status));
        break;
    default:
        status = ERR_UNSUPPORTED;
        (void)snprintf(response, capacity, "%s", error_to_string(status));
        break;
    }
    return status;
}
