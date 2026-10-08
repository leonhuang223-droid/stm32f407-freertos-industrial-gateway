#ifndef GATEWAY_UI_SUBSYSTEM_H
#define GATEWAY_UI_SUBSYSTEM_H

#include "config_subsystem.h"
#include "display_device.h"
#include "gateway_model.h"
#include "input_device.h"

#include <stddef.h>
#include <stdint.h>

#define UI_PAGE_COUNT 6u
#define UI_INTERNAL_DRAW_BUFFER_PIXELS (800u * 6u)

typedef enum {
    UI_POWER_ACTIVE = 0,
    UI_POWER_ECO,
    UI_POWER_SUSPENDED
} ui_power_state_t;

typedef enum {
    UI_PAGE_MONITOR = 0,
    UI_PAGE_MENU,
    UI_PAGE_POINT_DETAIL,
    UI_PAGE_ALARMS,
    UI_PAGE_DEVICES,
    UI_PAGE_PARAMETERS
} ui_page_id_t;

typedef enum {
    UI_ACTION_CONFIG_PATCH = 0,
    UI_ACTION_ACKNOWLEDGE_ALARM,
    UI_ACTION_OTA_START,
    UI_ACTION_OTA_CANCEL
} ui_action_type_t;

typedef struct {
    ui_action_type_t type;
    union {
        config_patch_t config_patch;
        uint32_t alarm_event_id;
    } payload;
} ui_action_t;

typedef status_t (*ui_action_handler_t)(void *context,
                                        const ui_action_t *action);

typedef struct {
    uint32_t refresh_count;
    uint32_t page_transitions;
    uint32_t action_errors;
    uint32_t flush_errors;
    uint32_t last_sequence;
    status_t last_error;
    ui_page_id_t active_page;
    ui_power_state_t power_state;
    uint8_t draw_buffer_count;
    uint8_t draw_buffer_degraded;
    uint8_t initialized;
} ui_health_t;

typedef struct ui_page ui_page_t;
typedef struct ui_subsystem ui_subsystem_t;

typedef struct {
    status_t (*create)(ui_page_t *page);
    status_t (*enter)(ui_page_t *page);
    status_t (*leave)(ui_page_t *page);
    status_t (*update)(ui_page_t *page);
    void (*destroy)(ui_page_t *page);
} ui_page_ops_t;

struct ui_page {
    const ui_page_ops_t *ops;
    ui_subsystem_t *owner;
    ui_page_id_t id;
    void *root;
    void *labels[6];
    uint8_t created;
};

struct ui_subsystem {
    display_device_t *display;
    input_device_t *input;
    ui_action_handler_t action_handler;
    void *action_context;
    gateway_system_snapshot_t snapshot;
    gateway_runtime_config_t runtime_config;
    ui_page_t pages[UI_PAGE_COUNT];
    ui_page_t *active_page;
    void *lv_display;
    void *lv_input;
    uint16_t internal_draw_buffer[UI_INTERNAL_DRAW_BUFFER_PIXELS];
    uint16_t *draw_buffer_primary;
    uint16_t *draw_buffer_secondary;
    size_t draw_buffer_pixels;
    uint32_t last_process_ms;
    uint8_t input_activity_pending;
    ui_health_t health;
};

status_t ui_subsystem_construct(ui_subsystem_t *subsystem,
                                display_device_t *display,
                                input_device_t *input,
                                ui_action_handler_t action_handler,
                                void *action_context);
status_t ui_subsystem_configure_draw_buffers(ui_subsystem_t *subsystem,
                                             uint16_t *primary,
                                             uint16_t *secondary,
                                             size_t pixels_per_buffer,
                                             uint8_t degraded);
status_t ui_subsystem_start(ui_subsystem_t *subsystem);
status_t ui_subsystem_set_action_handler(ui_subsystem_t *subsystem,
                                         ui_action_handler_t action_handler,
                                         void *action_context);
status_t ui_subsystem_process(ui_subsystem_t *subsystem,
                              uint32_t now_ms,
                              const gateway_system_snapshot_t *snapshot,
                              const gateway_runtime_config_t *config);
status_t ui_subsystem_navigate(ui_subsystem_t *subsystem, ui_page_id_t page);
status_t ui_subsystem_get_health(const ui_subsystem_t *subsystem,
                                 ui_health_t *health);
status_t ui_subsystem_set_power_state(ui_subsystem_t *subsystem,
                                      ui_power_state_t state);
uint8_t ui_subsystem_take_input_activity(ui_subsystem_t *subsystem);
const char *ui_page_name(ui_page_id_t page);

#endif
