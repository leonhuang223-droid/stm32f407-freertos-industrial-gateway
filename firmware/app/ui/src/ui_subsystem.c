#include "ui_subsystem.h"

#include "lvgl.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static uint8_t lvgl_initialized;

static status_t page_create(ui_page_t *page);
static status_t page_enter(ui_page_t *page);
static status_t page_leave(ui_page_t *page);
static status_t page_update(ui_page_t *page);
static void page_destroy(ui_page_t *page);

static const ui_page_ops_t page_ops = {
    page_create, page_enter, page_leave, page_update, page_destroy};

const char *ui_page_name(ui_page_id_t page)
{
    static const char *const names[UI_PAGE_COUNT] = {
        "Monitor", "Menu", "Point", "Alarms", "Devices", "Parameters"};

    return (unsigned int)page < UI_PAGE_COUNT ? names[(unsigned int)page]
                                              : "Unknown";
}

/** Synchronous request; pointed-to buffers remain caller-owned.
 * @author 兆鸣嵌入式
 */
typedef struct {
    lv_coord_t x;
    lv_coord_t y;
    const lv_font_t *font;
    uint32_t color;
} ui_label_style_t;

static lv_obj_t *add_label(lv_obj_t *parent,
                           const char *text,
                           const ui_label_style_t *parameters)
{
    if (parameters == 0) {
        return 0;
    }
    lv_coord_t x = parameters->x;
    lv_coord_t y = parameters->y;
    const lv_font_t *font = parameters->font;
    uint32_t color = parameters->color;

    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

/** Synchronous request; pointed-to buffers remain caller-owned.
 * @author 兆鸣嵌入式
 */
typedef struct {
    lv_coord_t x;
    lv_coord_t y;
    lv_coord_t width;
    lv_event_cb_t callback;
    void *user_data;
} ui_button_config_t;

static lv_obj_t *add_button(lv_obj_t *parent,
                            const char *text,
                            const ui_button_config_t *parameters)
{
    if (parameters == 0) {
        return 0;
    }
    lv_coord_t x = parameters->x;
    lv_coord_t y = parameters->y;
    lv_coord_t width = parameters->width;
    lv_event_cb_t callback = parameters->callback;
    void *user_data = parameters->user_data;

    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;

    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, 44);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x136B67), 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF4F7F6), 0);
    lv_obj_center(label);
    return button;
}

static void nav_event(lv_event_t *event)
{
    ui_page_t *target = lv_event_get_user_data(event);

    if (target != 0) {
        (void)ui_subsystem_navigate(target->owner, target->id);
    }
}

static void config_event(lv_event_t *event)
{
    ui_subsystem_t *subsystem = lv_event_get_user_data(event);
    ui_action_t action;
    status_t status = ERR_DEVICE_NOT_READY;

    if (subsystem == 0 || subsystem->runtime_config.rule_count == 0u) {
        return;
    }
    if (subsystem->runtime_config.rules[0].high_threshold > INT32_MAX - 100) {
        subsystem->health.last_error = ERR_INVALID_ARG;
        subsystem->health.action_errors++;
        return;
    }
    memset(&action, 0, sizeof(action));
    action.type = UI_ACTION_CONFIG_PATCH;
    action.payload.config_patch.point_id =
        subsystem->runtime_config.rules[0].point_id;
    action.payload.config_patch.field = CONFIG_FIELD_HIGH_THRESHOLD;
    action.payload.config_patch.value =
        subsystem->runtime_config.rules[0].high_threshold + 100;
    if (subsystem->action_handler != 0) {
        status = subsystem->action_handler(subsystem->action_context, &action);
    }
    subsystem->health.last_error = status;
    if (status != SYS_OK) {
        subsystem->health.action_errors++;
    }
}

static void style_screen(lv_obj_t *screen)
{
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x122326), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
}

static void add_status_bar(ui_page_t *page)
{
    lv_obj_t *root = page->root;
    lv_obj_t *bar = lv_obj_create(root);

    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, lv_pct(100), 36);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1D3639), 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    page->labels[0] = add_label(
        bar,
        ui_page_name(page->id),
        &(const ui_label_style_t){12, 8, &lv_font_montserrat_14, 0xE9F3F2});
    page->labels[1] = add_label(
        bar,
        "SEQ 0",
        &(const ui_label_style_t){150, 9, &lv_font_montserrat_14, 0xA9C7C4});
}

static void create_monitor(ui_page_t *page)
{
    lv_obj_t *root = page->root;

    page->labels[2] = add_label(
        root,
        "--",
        &(const ui_label_style_t){24, 62, &lv_font_montserrat_14, 0xF4C95D});
    page->labels[3] = add_label(
        root,
        "Point --",
        &(const ui_label_style_t){24, 102, &lv_font_montserrat_14, 0xE9F3F2});
    page->labels[4] = add_label(
        root,
        "Quality unavailable",
        &(const ui_label_style_t){24, 138, &lv_font_montserrat_14, 0xA9C7C4});
    page->labels[5] = add_label(
        root,
        "Alarm 0  Relay SAFE",
        &(const ui_label_style_t){24, 174, &lv_font_montserrat_14, 0xA9C7C4});
    (void)add_button(
        root,
        "Menu",
        &(const ui_button_config_t){
            24, 212, 112, nav_event, &page->owner->pages[UI_PAGE_MENU]});
}

static void create_menu(ui_page_t *page)
{
    static const ui_page_id_t targets[] = {UI_PAGE_MONITOR,
                                           UI_PAGE_POINT_DETAIL,
                                           UI_PAGE_ALARMS,
                                           UI_PAGE_DEVICES,
                                           UI_PAGE_PARAMETERS};
    lv_obj_t *root = page->root;
    unsigned int i;

    page->labels[2] = add_label(
        root,
        "Gateway views",
        &(const ui_label_style_t){24, 54, &lv_font_montserrat_14, 0xF4C95D});
    for (i = 0u; i < sizeof(targets) / sizeof(targets[0]); ++i) {
        lv_coord_t x = (lv_coord_t)(24 + (i % 2u) * 176u);
        lv_coord_t y = (lv_coord_t)(96 + (i / 2u) * 56u);
        (void)add_button(
            root,
            ui_page_name(targets[i]),
            &(const ui_button_config_t){
                x, y, 152, nav_event, &page->owner->pages[targets[i]]});
    }
}

static void create_detail(ui_page_t *page)
{
    lv_obj_t *root = page->root;

    page->labels[2] = add_label(
        root,
        "Point --",
        &(const ui_label_style_t){24, 56, &lv_font_montserrat_14, 0xF4C95D});
    page->labels[3] = add_label(
        root,
        "Engineering --",
        &(const ui_label_style_t){24, 98, &lv_font_montserrat_14, 0xE9F3F2});
    page->labels[4] = add_label(
        root,
        "Raw --",
        &(const ui_label_style_t){24, 134, &lv_font_montserrat_14, 0xA9C7C4});
    page->labels[5] = add_label(
        root,
        "Source -- / Quality --",
        &(const ui_label_style_t){24, 170, &lv_font_montserrat_14, 0xA9C7C4});
    (void)add_button(
        root,
        "Menu",
        &(const ui_button_config_t){
            24, 212, 112, nav_event, &page->owner->pages[UI_PAGE_MENU]});
}

static void create_alarms(ui_page_t *page)
{
    lv_obj_t *root = page->root;

    page->labels[2] = add_label(
        root,
        "Active alarms",
        &(const ui_label_style_t){24, 58, &lv_font_montserrat_14, 0xF4C95D});
    page->labels[3] = add_label(
        root,
        "0",
        &(const ui_label_style_t){24, 104, &lv_font_montserrat_14, 0xE9F3F2});
    page->labels[4] = add_label(
        root,
        "ACK does not clear a fault",
        &(const ui_label_style_t){24, 150, &lv_font_montserrat_14, 0xA9C7C4});
    (void)add_button(
        root,
        "Menu",
        &(const ui_button_config_t){
            24, 212, 112, nav_event, &page->owner->pages[UI_PAGE_MENU]});
}

static void create_devices(ui_page_t *page)
{
    lv_obj_t *root = page->root;

    page->labels[2] = add_label(
        root,
        "Subsystem flags",
        &(const ui_label_style_t){24, 58, &lv_font_montserrat_14, 0xF4C95D});
    page->labels[3] = add_label(
        root,
        "0x00000000",
        &(const ui_label_style_t){24, 104, &lv_font_montserrat_14, 0xE9F3F2});
    page->labels[4] = add_label(
        root,
        "Display controller: pending",
        &(const ui_label_style_t){24, 148, &lv_font_montserrat_14, 0xA9C7C4});
    (void)add_button(
        root,
        "Menu",
        &(const ui_button_config_t){
            24, 212, 112, nav_event, &page->owner->pages[UI_PAGE_MENU]});
}

static void create_parameters(ui_page_t *page)
{
    lv_obj_t *root = page->root;

    page->labels[2] = add_label(
        root,
        "Runtime configuration",
        &(const ui_label_style_t){24, 54, &lv_font_montserrat_14, 0xF4C95D});
    page->labels[3] = add_label(
        root,
        "Revision 0",
        &(const ui_label_style_t){24, 94, &lv_font_montserrat_14, 0xE9F3F2});
    page->labels[4] = add_label(
        root,
        "Rule -- High --",
        &(const ui_label_style_t){24, 128, &lv_font_montserrat_14, 0xA9C7C4});
    (void)add_button(
        root,
        "High +100",
        &(const ui_button_config_t){24, 170, 152, config_event, page->owner});
    (void)add_button(
        root,
        "Menu",
        &(const ui_button_config_t){
            200, 170, 112, nav_event, &page->owner->pages[UI_PAGE_MENU]});
}

static status_t page_create(ui_page_t *page)
{
    lv_obj_t *root;

    if (page == 0 || page->owner == 0 ||
        (unsigned int)page->id >= UI_PAGE_COUNT) {
        return ERR_INVALID_ARG;
    }
    if (page->created != 0u) {
        return SYS_OK;
    }
    root = lv_obj_create(0);
    if (root == 0) {
        return ERR_NO_MEMORY;
    }
    page->root = root;
    style_screen(root);
    add_status_bar(page);
    switch (page->id) {
    case UI_PAGE_MONITOR:
        create_monitor(page);
        break;
    case UI_PAGE_MENU:
        create_menu(page);
        break;
    case UI_PAGE_POINT_DETAIL:
        create_detail(page);
        break;
    case UI_PAGE_ALARMS:
        create_alarms(page);
        break;
    case UI_PAGE_DEVICES:
        create_devices(page);
        break;
    case UI_PAGE_PARAMETERS:
        create_parameters(page);
        break;
    default:
        return ERR_UNSUPPORTED;
    }
    page->created = 1u;
    return SYS_OK;
}

static status_t page_enter(ui_page_t *page)
{
    status_t status = page_create(page);

    if (status == SYS_OK) {
        lv_screen_load(page->root);
        lv_obj_invalidate(page->root);
    }
    return status;
}

static status_t page_leave(ui_page_t *page)
{
    return page != 0 && page->created != 0u ? SYS_OK : ERR_INVALID_ARG;
}

static const char *quality_name(gateway_quality_t quality)
{
    static const char *const names[] = {"good",
                                        "stale",
                                        "comm error",
                                        "out of range",
                                        "sensor fault",
                                        "unavailable"};

    return (unsigned int)quality < sizeof(names) / sizeof(names[0])
               ? names[(unsigned int)quality]
               : "invalid";
}

static status_t page_update(ui_page_t *page)
{
    const gateway_system_snapshot_t *snapshot;
    const gateway_runtime_config_t *config;

    if (page == 0 || page->created == 0u || page->owner == 0) {
        return ERR_INVALID_ARG;
    }
    snapshot = &page->owner->snapshot;
    config = &page->owner->runtime_config;
    lv_label_set_text_fmt(
        page->labels[1], "SEQ %lu", (unsigned long)snapshot->sequence);
    switch (page->id) {
    case UI_PAGE_MONITOR:
        lv_label_set_text_fmt(
            page->labels[2], "%ld", (long)snapshot->latest.engineering_value);
        lv_label_set_text_fmt(page->labels[3],
                              "Point %u",
                              (unsigned int)snapshot->latest.point_id);
        lv_label_set_text_fmt(page->labels[4],
                              "Quality %s",
                              quality_name(snapshot->latest.quality));
        lv_label_set_text_fmt(page->labels[5],
                              "Alarm %lu  Relay %s",
                              (unsigned long)snapshot->active_alarm_count,
                              snapshot->relay_energized != 0u ? "ON" : "SAFE");
        break;
    case UI_PAGE_POINT_DETAIL:
        lv_label_set_text_fmt(page->labels[2],
                              "Point %u",
                              (unsigned int)snapshot->latest.point_id);
        lv_label_set_text_fmt(page->labels[3],
                              "Engineering %ld",
                              (long)snapshot->latest.engineering_value);
        lv_label_set_text_fmt(
            page->labels[4], "Raw %ld", (long)snapshot->latest.raw_value);
        lv_label_set_text_fmt(page->labels[5],
                              "Source %u / Quality %s",
                              (unsigned int)snapshot->latest.source,
                              quality_name(snapshot->latest.quality));
        break;
    case UI_PAGE_ALARMS:
        lv_label_set_text_fmt(page->labels[3],
                              "%lu",
                              (unsigned long)snapshot->active_alarm_count);
        break;
    case UI_PAGE_DEVICES:
        lv_label_set_text_fmt(
            page->labels[3], "0x%08lX", (unsigned long)snapshot->system_flags);
        break;
    case UI_PAGE_PARAMETERS:
        lv_label_set_text_fmt(
            page->labels[3], "Revision %lu", (unsigned long)config->revision);
        if (config->rule_count != 0u) {
            lv_label_set_text_fmt(page->labels[4],
                                  "Rule %u High %ld",
                                  (unsigned int)config->rules[0].point_id,
                                  (long)config->rules[0].high_threshold);
        }
        break;
    case UI_PAGE_MENU:
        break;
    default:
        return ERR_UNSUPPORTED;
    }
    return SYS_OK;
}

static void page_destroy(ui_page_t *page)
{
    if (page != 0 && page->created != 0u) {
        lv_obj_delete(page->root);
        page->root = 0;
        memset(page->labels, 0, sizeof(page->labels));
        page->created = 0u;
    }
}

static void display_flush_callback(lv_display_t *lv_display,
                                   const lv_area_t *area,
                                   uint8_t *pixels)
{
    ui_subsystem_t *subsystem = lv_display_get_user_data(lv_display);
    display_area_t target;
    size_t pixel_count;
    status_t status;

    target.x1 = (int16_t)area->x1;
    target.y1 = (int16_t)area->y1;
    target.x2 = (int16_t)area->x2;
    target.y2 = (int16_t)area->y2;
    pixel_count =
        (size_t)(area->x2 - area->x1 + 1) * (size_t)(area->y2 - area->y1 + 1);
    status = display_device_flush(
        subsystem->display, &target, (const uint16_t *)pixels, pixel_count);
    subsystem->health.last_error = status;
    if (status != SYS_OK) {
        subsystem->health.flush_errors++;
    }
    lv_display_flush_ready(lv_display);
}

static void input_read_callback(lv_indev_t *lv_input, lv_indev_data_t *data)
{
    ui_subsystem_t *subsystem = lv_indev_get_user_data(lv_input);
    input_sample_t sample;

    if (subsystem == 0 || subsystem->input == 0 ||
        input_device_read(subsystem->input, &sample) != SYS_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    data->point.x = sample.x;
    data->point.y = sample.y;
    data->state = sample.state == INPUT_STATE_PRESSED ? LV_INDEV_STATE_PRESSED
                                                      : LV_INDEV_STATE_RELEASED;
    if (sample.state == INPUT_STATE_PRESSED) {
        subsystem->input_activity_pending = 1u;
    }
}

status_t ui_subsystem_construct(ui_subsystem_t *subsystem,
                                display_device_t *display,
                                input_device_t *input,
                                ui_action_handler_t action_handler,
                                void *action_context)
{
    unsigned int i;

    if (subsystem == 0 || display == 0) {
        return ERR_INVALID_ARG;
    }
    memset(subsystem, 0, sizeof(*subsystem));
    subsystem->display = display;
    subsystem->input = input;
    subsystem->action_handler = action_handler;
    subsystem->action_context = action_context;
    subsystem->draw_buffer_primary = subsystem->internal_draw_buffer;
    subsystem->draw_buffer_pixels = UI_INTERNAL_DRAW_BUFFER_PIXELS;
    subsystem->snapshot.latest.quality = GATEWAY_QUALITY_UNAVAILABLE;
    subsystem->health.last_error = ERR_DEVICE_NOT_READY;
    subsystem->health.power_state = UI_POWER_ACTIVE;
    subsystem->health.draw_buffer_count = 1u;
    for (i = 0u; i < UI_PAGE_COUNT; ++i) {
        subsystem->pages[i].ops = &page_ops;
        subsystem->pages[i].owner = subsystem;
        subsystem->pages[i].id = (ui_page_id_t)i;
    }
    return SYS_OK;
}

status_t ui_subsystem_configure_draw_buffers(ui_subsystem_t *subsystem,
                                             uint16_t *primary,
                                             uint16_t *secondary,
                                             size_t pixels_per_buffer,
                                             uint8_t degraded)
{
    if (subsystem == 0 || subsystem->health.initialized != 0u) {
        return ERR_INVALID_ARG;
    }
    if (primary == 0) {
        if (secondary != 0 || pixels_per_buffer != 0u) {
            return ERR_INVALID_ARG;
        }
        subsystem->draw_buffer_primary = subsystem->internal_draw_buffer;
        subsystem->draw_buffer_secondary = 0;
        subsystem->draw_buffer_pixels = UI_INTERNAL_DRAW_BUFFER_PIXELS;
        subsystem->health.draw_buffer_count = 1u;
        subsystem->health.draw_buffer_degraded = degraded != 0u ? 1u : 0u;
        return SYS_OK;
    }
    if (pixels_per_buffer < subsystem->display->width) {
        return ERR_INVALID_ARG;
    }
    subsystem->draw_buffer_primary = primary;
    subsystem->draw_buffer_secondary = secondary;
    subsystem->draw_buffer_pixels = pixels_per_buffer;
    subsystem->health.draw_buffer_count = secondary != 0 ? 2u : 1u;
    subsystem->health.draw_buffer_degraded = degraded != 0u ? 1u : 0u;
    return SYS_OK;
}

status_t ui_subsystem_start(ui_subsystem_t *subsystem)
{
    lv_display_t *lv_display;
    lv_indev_t *lv_input;
    status_t status;

    if (subsystem == 0 || subsystem->display == 0) {
        return ERR_INVALID_ARG;
    }
    status = display_device_init(subsystem->display);
    if (status != SYS_OK) {
        subsystem->health.last_error = status;
        return status;
    }
    if (lvgl_initialized == 0u) {
        lv_init();
        lvgl_initialized = 1u;
    }
    lv_display = lv_display_create(subsystem->display->width,
                                   subsystem->display->height);
    if (lv_display == 0) {
        subsystem->health.last_error = ERR_NO_MEMORY;
        return ERR_NO_MEMORY;
    }
    subsystem->lv_display = lv_display;
    lv_display_set_user_data(lv_display, subsystem);
    lv_display_set_color_format(lv_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(lv_display, display_flush_callback);
    lv_display_set_buffers(lv_display,
                           subsystem->draw_buffer_primary,
                           subsystem->draw_buffer_secondary,
                           subsystem->draw_buffer_pixels * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    if (subsystem->input != 0 &&
        input_device_init(subsystem->input) == SYS_OK) {
        lv_input = lv_indev_create();
        if (lv_input == 0) {
            subsystem->health.last_error = ERR_NO_MEMORY;
            return ERR_NO_MEMORY;
        }
        subsystem->lv_input = lv_input;
        lv_indev_set_type(lv_input, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(lv_input, lv_display);
        lv_indev_set_user_data(lv_input, subsystem);
        lv_indev_set_read_cb(lv_input, input_read_callback);
    }
    status = ui_subsystem_set_power_state(subsystem, UI_POWER_ACTIVE);
    if (status == SYS_OK) {
        status = ui_subsystem_navigate(subsystem, UI_PAGE_MONITOR);
    }
    subsystem->health.initialized = status == SYS_OK ? 1u : 0u;
    subsystem->health.last_error = status;
    return status;
}

status_t ui_subsystem_set_action_handler(ui_subsystem_t *subsystem,
                                         ui_action_handler_t action_handler,
                                         void *action_context)
{
    if (subsystem == 0 || action_handler == 0) {
        return ERR_INVALID_ARG;
    }
    subsystem->action_handler = action_handler;
    subsystem->action_context = action_context;
    return SYS_OK;
}

status_t ui_subsystem_navigate(ui_subsystem_t *subsystem, ui_page_id_t page)
{
    ui_page_t *target;
    status_t status;

    if (subsystem == 0 || (unsigned int)page >= UI_PAGE_COUNT ||
        subsystem->lv_display == 0) {
        return ERR_INVALID_ARG;
    }
    target = &subsystem->pages[(unsigned int)page];
    if (subsystem->active_page == target) {
        return SYS_OK;
    }
    if (subsystem->active_page != 0) {
        status = subsystem->active_page->ops->leave(subsystem->active_page);
        if (status != SYS_OK) {
            return status;
        }
    }
    status = target->ops->enter(target);
    if (status == SYS_OK) {
        subsystem->active_page = target;
        subsystem->health.active_page = page;
        subsystem->health.page_transitions++;
    }
    subsystem->health.last_error = status;
    return status;
}

status_t ui_subsystem_process(ui_subsystem_t *subsystem,
                              uint32_t now_ms,
                              const gateway_system_snapshot_t *snapshot,
                              const gateway_runtime_config_t *config)
{
    uint32_t elapsed;
    status_t status;

    if (subsystem == 0 || snapshot == 0 || config == 0 ||
        subsystem->health.initialized == 0u || subsystem->active_page == 0) {
        return ERR_DEVICE_NOT_READY;
    }
    subsystem->snapshot = *snapshot;
    subsystem->runtime_config = *config;
    elapsed = now_ms - subsystem->last_process_ms;
    if (elapsed != 0u) {
        lv_tick_inc(elapsed);
        subsystem->last_process_ms = now_ms;
    }
    status = subsystem->active_page->ops->update(subsystem->active_page);
    if (status == SYS_OK) {
        (void)lv_timer_handler();
        subsystem->health.refresh_count++;
        subsystem->health.last_sequence = snapshot->sequence;
    }
    subsystem->health.last_error = status;
    return status;
}

status_t ui_subsystem_get_health(const ui_subsystem_t *subsystem,
                                 ui_health_t *health)
{
    if (subsystem == 0 || health == 0) {
        return ERR_INVALID_ARG;
    }
    *health = subsystem->health;
    return SYS_OK;
}

status_t ui_subsystem_set_power_state(ui_subsystem_t *subsystem,
                                      ui_power_state_t state)
{
    status_t status = SYS_OK;

    if (subsystem == 0 || subsystem->display == 0 ||
        (unsigned int)state > UI_POWER_SUSPENDED) {
        return ERR_INVALID_ARG;
    }
    if (state == subsystem->health.power_state && state != UI_POWER_ACTIVE) {
        return SYS_OK;
    }
    if (state == UI_POWER_SUSPENDED) {
        if (subsystem->input != 0) {
            status = input_device_suspend(subsystem->input);
        }
        if (status == SYS_OK) {
            status = display_device_suspend(subsystem->display);
            if (status != SYS_OK && subsystem->input != 0) {
                /* A display failure must not strand the touch device asleep. */
                status_t restore_status = input_device_resume(subsystem->input);
                if (restore_status != SYS_OK) {
                    subsystem->health.power_state = UI_POWER_SUSPENDED;
                    status = restore_status;
                }
            }
        }
    } else {
        if (subsystem->health.power_state == UI_POWER_SUSPENDED) {
            status = display_device_resume(subsystem->display);
            if (status == SYS_OK && subsystem->input != 0) {
                status = input_device_resume(subsystem->input);
            }
        }
        if (status == SYS_OK) {
            status = display_device_set_backlight(
                subsystem->display, state == UI_POWER_ACTIVE ? 80u : 20u);
        }
    }
    if (status == SYS_OK) {
        subsystem->health.power_state = state;
    }
    subsystem->health.last_error = status;
    return status;
}

uint8_t ui_subsystem_take_input_activity(ui_subsystem_t *subsystem)
{
    uint8_t activity;

    if (subsystem == 0) {
        return 0u;
    }
    activity = subsystem->input_activity_pending;
    subsystem->input_activity_pending = 0u;
    return activity;
}
