#include "runtime_internal.h"
#include <stdio.h>
#include <string.h>

static status_t handle_ui_action(void *opaque, const ui_action_t *action);

typedef struct {
    gateway_system_snapshot_t snapshot;
    gateway_runtime_config_t runtime_config;
    status_t startup_status;
    uint8_t ui_lock_held;
    uint8_t power_suspended;
    uint32_t ui_lock_release_ms;
    ui_power_state_t applied_power_state;
} ui_task_state_t;

static int handle_ui_power(app_ui_task_context_t *context,
                           ui_task_state_t *state,
                           EventBits_t power_bits)
{
    if ((power_bits & SYSTEM_EVENT_POWER_QUIESCE_REQUEST) != 0u) {
        if (state->power_suspended == 0u) {
            status_t status = state->startup_status == SYS_OK
                                  ? ui_subsystem_set_power_state(
                                        context->ui, UI_POWER_SUSPENDED)
                                  : SYS_OK;

            /* A failed suspend may still require peripheral rollback. */
            state->power_suspended = 1u;
            if (status == SYS_OK) {
                if (state->ui_lock_held != 0u) {
                    (void)power_lock_release(PM_LOCK_UI_ACTIVE);
                    state->ui_lock_held = 0u;
                }
                state->applied_power_state = UI_POWER_SUSPENDED;
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_POWER_ACK_UI);
            } else {
                xEventGroupSetBits(channels.system_events,
                                   SYSTEM_EVENT_FAULT_ACTIVE);
            }
        }
        app_runtime_mark_alive(GATEWAY_TASK_UI);
        vTaskDelay(pdMS_TO_TICKS(20u));
        return 1;
    }
    if (state->power_suspended != 0u) {
        status_t status = state->startup_status == SYS_OK
                              ? ui_subsystem_set_power_state(
                                    context->ui,
                                    context->power->current_mode == POWER_ECO
                                        ? UI_POWER_ECO
                                        : UI_POWER_ACTIVE)
                              : SYS_OK;

        if (status == SYS_OK) {
            state->applied_power_state =
                context->power->current_mode == POWER_ECO ? UI_POWER_ECO
                                                          : UI_POWER_ACTIVE;
        } else {
            xEventGroupSetBits(channels.system_events,
                               SYSTEM_EVENT_FAULT_ACTIVE);
        }
        xEventGroupClearBits(channels.system_events, SYSTEM_EVENT_POWER_ACK_UI);
        state->power_suspended = status == SYS_OK ? 0u : 1u;
    }

    if (state->power_suspended != 0u) {
        app_runtime_mark_alive(GATEWAY_TASK_UI);
        vTaskDelay(pdMS_TO_TICKS(20u));
        return 1;
    }
    return 0;
}

static void service_ui(app_ui_task_context_t *context, ui_task_state_t *state)
{
    ui_page_id_t page;

    if (state->startup_status == SYS_OK) {
        const uint32_t now_ms =
            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        power_mode_t power_mode;
        ui_power_state_t requested_power_state;

        (void)xQueueReceive(channels.ui_snapshot, &state->snapshot, 0U);
        while (xQueueReceive(channels.ui_command, &page, 0u) == pdPASS) {
            (void)ui_subsystem_navigate(context->ui, page);
        }
        if (app_config_read(&state->runtime_config, 0, 5u) == SYS_OK) {
            (void)ui_subsystem_process(
                context->ui, now_ms, &state->snapshot, &state->runtime_config);
        }
        if (ui_subsystem_take_input_activity(context->ui) != 0u) {
            app_critical_enter();
            power_manager_note_activity(context->power, now_ms);
            app_critical_exit();
            if (state->ui_lock_held == 0u &&
                power_lock_acquire(PM_LOCK_UI_ACTIVE) == SYS_OK) {
                state->ui_lock_held = 1u;
            }
            state->ui_lock_release_ms = now_ms + UI_ACTIVE_LOCK_HOLD_MS;
        }
        if (state->ui_lock_held != 0u &&
            (int32_t)(now_ms - state->ui_lock_release_ms) >= 0 &&
            power_lock_release(PM_LOCK_UI_ACTIVE) == SYS_OK) {
            state->ui_lock_held = 0u;
        }
        app_critical_enter();
        power_mode = context->power->current_mode;
        app_critical_exit();
        requested_power_state =
            state->ui_lock_held != 0u || power_mode == POWER_ACTIVE
                ? UI_POWER_ACTIVE
                : (power_mode == POWER_ECO ? UI_POWER_ECO : UI_POWER_SUSPENDED);
        if (requested_power_state != state->applied_power_state &&
            ui_subsystem_set_power_state(context->ui, requested_power_state) ==
                SYS_OK) {
            state->applied_power_state = requested_power_state;
        }
    }
}

void ui_task(void *argument)
{
    app_ui_task_context_t *context = argument;
    ui_task_state_t state = {0};
    state.applied_power_state = UI_POWER_ACTIVE;

    (void)app_runtime_read_snapshot(&state.snapshot);
    (void)app_config_read(&state.runtime_config, 0, 5u);
    (void)ui_subsystem_set_action_handler(
        context->ui, handle_ui_action, context);
    state.startup_status = ui_subsystem_start(context->ui);
    (*context->ui_startup_status) = state.startup_status;
    if (state.startup_status == SYS_OK) {
        xEventGroupSetBits(channels.system_events, SYSTEM_EVENT_UI_READY);
    } else {
        xEventGroupClearBits(channels.system_events, SYSTEM_EVENT_UI_READY);
    }

    for (;;) {
        uint32_t notification_value;
        EventBits_t power_bits = xEventGroupGetBits(channels.system_events);

        if (handle_ui_power(context, &state, power_bits)) {
            continue;
        }

        service_ui(context, &state);
        app_runtime_mark_alive(GATEWAY_TASK_UI);
        (void)xTaskNotifyWait(
            0u,
            UINT32_MAX,
            &notification_value,
            pdMS_TO_TICKS(state.startup_status == SYS_OK ? 10u : 1000u));
    }
}

static status_t handle_ui_action(void *opaque, const ui_action_t *action)
{
    app_ui_task_context_t *context = opaque;

    if (context == 0 || action == 0) {
        return ERR_INVALID_ARG;
    }
    if (action->type == UI_ACTION_CONFIG_PATCH) {
        return submit_config_patch(&action->payload.config_patch, 0);
    }
    if (action->type == UI_ACTION_OTA_START ||
        action->type == UI_ACTION_OTA_CANCEL) {
        gateway_ota_command_t command;

        command.type = action->type == UI_ACTION_OTA_START
                           ? GATEWAY_OTA_COMMAND_START
                           : GATEWAY_OTA_COMMAND_CANCEL;
        command.request_id = (*context->heartbeat)[GATEWAY_TASK_UI];
        return app_rtos_submit_ota_command(&command);
    }
    return ERR_UNSUPPORTED;
}
