#include "runtime_internal.h"
#include "mock_rtos.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static app_context_t app;
static app_storage_task_state_t storage_state;
static app_storage_task_context_t *storage_context;
static int relay_level;
static unsigned int wake_calls;

static status_t relay_init_mock(void *opaque, int inactive)
{ *(int *)opaque = inactive; return SYS_OK; }
static status_t relay_write_mock(void *opaque, int active)
{ *(int *)opaque = active; return SYS_OK; }
static status_t wake_failure(void *opaque)
{ (void)opaque; wake_calls++; return ERR_IO; }

static void setup(void)
{
    gateway_runtime_config_t config = { 0 };
    const relay_ops_t relay_ops = { .init = relay_init_mock, .write = relay_write_mock };
    const relay_config_t relay_config = { 1u, RELAY_DEENERGIZED };
    const power_manager_config_t power_config = { 1000u, 5u, 100u, 30000u, 0u };
    /* Keep ops alive for the full test, unlike the local configuration value. */
    static relay_ops_t permanent_relay_ops;
    permanent_relay_ops = relay_ops;
    mock_rtos_reset();
    assert(app_context_init(&app) == SYS_OK);
    assert(power_manager_construct(&app.power, &power_config, 0u) == SYS_OK);
    config.schema_version = GATEWAY_RUNTIME_CONFIG_SCHEMA_VERSION;
    config.revision = 1u;
    config.rule_count = 1u;
    config.rules[0].point_id = GATEWAY_POINT_LOOP_CURRENT;
    config.rules[0].high_enabled = 1u;
    config.rules[0].high_threshold = 20000;
    config.rules[0].assert_samples = 1u;
    config.rules[0].recover_samples = 1u;
    assert(config_subsystem_construct(&app.config, &config) == SYS_OK);
    assert(relay_construct(&app.relay, &permanent_relay_ops, &relay_level, &relay_config) == SYS_OK);
    assert(alarm_subsystem_construct(&app.alarm, &app.relay, &config) == SYS_OK);
    assert(alarm_subsystem_start(&app.alarm) == SYS_OK);
    /* The harness creates tasks but deliberately does not run a scheduler. */
    assert(app_rtos_start(&app) == ERR_NO_MEMORY);
    storage_context = app_task_context_get(GATEWAY_TASK_STORAGE);
    memset(&storage_state, 0, sizeof(storage_state));
    wake_calls = 0u;
}

static void assert_consumed(void)
{
    QueueHandle_t members[] = { channels.storage_log, channels.storage_alarm,
        channels.storage_config, channels.ota_storage_request };
    size_t i;
    assert(uxQueueMessagesWaiting(channels.storage_set) == 0u);
    for (i = 0u; i < sizeof(members) / sizeof(members[0]); ++i) {
        assert(members[i]->count == 0u && members[i]->selected == 0u);
    }
}

static void test_storage_failure_consumes_selected_members(unsigned int mode)
{
    ota_storage_request_t ota = { 0 };
    ota_storage_request_t *pointer = &ota;
    gateway_storage_config_request_t config;
    gateway_storage_log_request_t log = { 0 };
    gateway_storage_alarm_request_t alarm = { 0 };
    config_patch_t patch = { GATEWAY_POINT_LOOP_CURRENT, CONFIG_FIELD_HIGH_THRESHOLD, 22000 };
    static const storage_media_ops_t media_ops = { .wake = wake_failure };
    setup();
    if (mode != 0u) {
        app.storage.health.mounted = 1u;
        app.storage.health.powered_down = mode == 1u ? 1u : 0u;
        app.storage.media = &app.storage_media;
        app.storage_media.ops = &media_ops;
        if (mode == 2u) {
            app.power.lock_refcount[PM_LOCK_FLASH_WRITE] = UINT16_MAX;
        }
    }
    ota.operation = OTA_STORAGE_WRITE;
    ota.waiter = xTaskGetCurrentTaskHandle();
    assert(request_lifecycle_submit(&ota.lifecycle, 0u, 5000u) == SYS_OK);
    assert(config_subsystem_prepare(&app.config, &patch, &config) == SYS_OK);
    assert(xQueueSend(channels.storage_config, &config, 0u) == pdPASS);
    assert(xQueueSend(channels.storage_log, &log, 0u) == pdPASS);
    assert(xQueueSend(channels.storage_alarm, &alarm, 0u) == pdPASS);
    assert(xQueueSend(channels.ota_storage_request, &pointer, 0u) == pdPASS);
    app_storage_task_step(storage_context, &storage_state, 0u);
    assert_consumed();
    assert(ota.lifecycle.state == REQUEST_COMPLETED);
    assert(ota.lifecycle.result == ERR_DEVICE_NOT_READY);
    assert(app.config.health.pending_requests == 0u);
    assert(app.config.active.rules[0].high_threshold == 20000);
    assert(app.alarm.config.rules[0].high_threshold == 20000);
    assert(app.storage_log_publish_drops == 1u && app.storage_config_publish_drops == 1u);
    if (mode == 1u) { assert(wake_calls == 1u); }
}

static void run_storage_worker(void)
{
    app_storage_task_step(storage_context, &storage_state, 0u);
}

static void test_adapter_timeout_and_late_completion(void)
{
    uint8_t bytes[] = { 1u, 2u, 3u, 4u };
    ota_rtos_port_t port;
    setup();
    port.context = app_task_context_get(GATEWAY_TASK_OTA);
    port.cancel_seen = 0u;
    assert(ota_port_staging_write(&port, 0u, bytes, sizeof(bytes)) == ERR_TIMEOUT);
    assert(mock_ticks == OTA_STORAGE_IO_TIMEOUT_MS);
    memset(bytes, 9, sizeof(bytes));
    /* A second request cannot recycle memory still owned by the late worker. */
    assert(ota_port_staging_write(&port, 0u, bytes, sizeof(bytes)) == ERR_DEVICE_NOT_READY);
    run_storage_worker(); /* Expired request is rejected before accessing flash. */
    assert_consumed();
    mock_wait_hook = run_storage_worker;
    assert(ota_port_staging_write(&port, 0u, bytes, sizeof(bytes)) == ERR_DEVICE_NOT_READY);
    assert_consumed();
    assert(app.heartbeat[GATEWAY_TASK_OTA] != 0u);
}

static void test_network_early_error_and_expired_lease(void)
{
    app_network_task_state_t state;
    app_network_task_context_t *context;
    ota_network_request_t request = { 0 };
    ota_network_request_t *pointer = &request;
    gateway_network_control_request_t lease = {
        GATEWAY_NETWORK_CONTROL_OTA_ACQUIRE, 123u, 1u
    };
    gateway_network_control_result_t result;
    setup();
    context = app_task_context_get(GATEWAY_TASK_NETWORK);
    app_network_task_init(context, &state);
    assert(request_lifecycle_submit(&request.lifecycle, 0u, 5000u) == SYS_OK);
    assert(xQueueSend(channels.ota_network_request, &pointer, 0u) == pdPASS);
    assert(xQueueSend(channels.network_control, &lease, 0u) == pdPASS);
    mock_ticks = 10u;
    app_network_task_step(context, &state);
    assert(request.lifecycle.state == REQUEST_COMPLETED);
    assert(request.lifecycle.result == ERR_DEVICE_NOT_READY);
    assert(xQueueReceive(channels.network_control_result, &result, 0u) == pdPASS);
    assert(result.request_id == 123u && result.status == ERR_TIMEOUT);
    assert(app.network.health.ota_lease_active == 0u);
}

static void test_config_publish_failure_rolls_back(void)
{
    gateway_storage_config_request_t filler = { 0 };
    config_patch_t patch = { GATEWAY_POINT_LOOP_CURRENT, CONFIG_FIELD_HIGH_THRESHOLD, 23000 };
    unsigned int i;
    setup();
    for (i = 0u; i < STORAGE_CONFIG_QUEUE_LENGTH; ++i) {
        assert(xQueueSend(channels.storage_config, &filler, 0u) == pdPASS);
    }
    assert(submit_config_patch(&patch, 0) == ERR_QUEUE_FULL);
    assert(app.config.health.pending_requests == 0u);
    assert(app.config.active.rules[0].high_threshold == 20000);
    run_storage_worker();
    assert_consumed();
}

typedef struct { uint8_t response[32]; size_t length; unsigned int joins; } offline_serial_t;
static status_t serial_ok(void *opaque) { (void)opaque; return SYS_OK; }
static status_t serial_write(void *opaque, const uint8_t *data, size_t length,
                              uint32_t timeout_ms)
{
    offline_serial_t *serial = opaque;
    (void)timeout_ms;
    if (length >= 8u && memcmp(data, "AT+CWJAP", 8u) == 0) {
        serial->joins++;
    } else {
        memcpy(serial->response, "OK\r\n", 4u);
        serial->length = 4u;
    }
    return SYS_OK;
}
static status_t serial_read(void *opaque, uint8_t *data, size_t capacity,
                             size_t *length, uint32_t timeout_ms)
{
    offline_serial_t *serial = opaque;
    if (serial->length == 0u) { *length = 0u; mock_ticks += timeout_ms; return ERR_TIMEOUT; }
    *length = serial->length < capacity ? serial->length : capacity;
    memcpy(data, serial->response, *length);
    memmove(serial->response, serial->response + *length, serial->length - *length);
    serial->length -= *length;
    return SYS_OK;
}
static uint32_t serial_now(void *opaque) { (void)opaque; return mock_ticks; }
static status_t watchdog_start(void *opaque, uint32_t timeout)
{ (void)opaque; assert(timeout == 12000u); return SYS_OK; }
static status_t watchdog_refresh(void *opaque) { (*(unsigned int *)opaque)++; return SYS_OK; }
static uint32_t watchdog_remaining(const void *opaque) { (void)opaque; return 12000u; }

static void test_offline_network_is_alive_and_stalled_task_is_detected(void)
{
    offline_serial_t serial = { 0 };
    unsigned int refreshes = 0u;
    app_network_config_t network_config = { 0 };
    app_network_task_state_t state;
    app_network_task_context_t *context;
    supervisor_config_t supervisor_config = { 0 };
    static const esp8266_serial_ops_t serial_ops = {
        .init = serial_ok, .write = serial_write, .read = serial_read,
        .suspend = serial_ok, .resume = serial_ok, .now_ms = serial_now
    };
    static const watchdog_device_ops_t watchdog_ops = {
        watchdog_start, watchdog_refresh, watchdog_remaining
    };
    setup();
    network_config.serial_ops = &serial_ops;
    network_config.serial_context = &serial;
    network_config.esp8266 = (esp8266_config_t){ "ssid", "password", 1000u, 15000u, 0u };
    network_config.network.device_id = "test";
    network_config.network.client_id = "test";
    network_config.network.username = "";
    network_config.network.password = "";
    network_config.network.broker_host = "server";
    network_config.network.broker_port = 1883u;
    network_config.network.keep_alive_seconds = 30u;
    network_config.network.connect_timeout_ms = 3000u;
    network_config.network.puback_timeout_ms = 3000u;
    network_config.network.reconnect_initial_ms = 1000u;
    network_config.network.reconnect_max_ms = 30000u;
    network_config.network.publish_retry_limit = 2u;
    network_config.ota_manifest_url = "http://server/manifest.json";
    network_config.ota_http_timeout_ms = 1000u;
    assert(app_context_configure_network(&app, &network_config) == SYS_OK);
    assert(watchdog_device_construct(&app.watchdog, &watchdog_ops, &refreshes) == SYS_OK);
    assert(watchdog_device_start(&app.watchdog, 12000u) == SYS_OK);
    supervisor_config.task_count = GATEWAY_TASK_COUNT;
    supervisor_config.critical_task_mask = 1u << GATEWAY_TASK_NETWORK;
    supervisor_config.task_timeout_ms[GATEWAY_TASK_NETWORK] = 2500u;
    supervisor_config.startup_grace_ms = 3500u;
    supervisor_config.boot_confirm_stable_ms = 1000u;
    assert(supervisor_subsystem_construct(&app.supervisor, &app.watchdog,
                                          &supervisor_config) == SYS_OK);
    assert(supervisor_subsystem_start(&app.supervisor, mock_ticks, app.heartbeat) == SYS_OK);
    context = app_task_context_get(GATEWAY_TASK_NETWORK);
    app_network_task_init(context, &state);
    assert(mock_ticks == 0u); /* Initialization schedules, rather than waiting for Wi-Fi. */
    while (mock_ticks < 15500u) {
        uint32_t before = mock_ticks;
        app_network_task_step(context, &state);
        assert(mock_ticks - before <= 10u);
        assert(supervisor_subsystem_process(&app.supervisor, mock_ticks,
                                            app.heartbeat, 1u) == SYS_OK);
        mock_ticks += 100u;
    }
    assert(serial.joins == 1u && refreshes > 100u);
    assert(app.network.health.mqtt_ready == 0u && app.supervisor.health.stale_task_mask == 0u);
    mock_ticks += 2600u; /* No owner step: a genuine task stall remains a fault. */
    assert(supervisor_subsystem_process(&app.supervisor, mock_ticks,
                                        app.heartbeat, 1u) == ERR_TIMEOUT);
    assert(app.supervisor.health.stale_task_mask == (1u << GATEWAY_TASK_NETWORK));
}

int main(void)
{
    test_storage_failure_consumes_selected_members(0u);
    test_storage_failure_consumes_selected_members(1u);
    test_storage_failure_consumes_selected_members(2u);
    test_adapter_timeout_and_late_completion();
    test_network_early_error_and_expired_lease();
    test_config_publish_failure_rolls_back();
    test_offline_network_is_alive_and_stalled_task_is_detected();
    puts("RTOS task fault paths passed");
    return 0;
}
