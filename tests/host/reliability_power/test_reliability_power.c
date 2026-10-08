#include "boot_confirmation.h"
#include "boot_metadata.h"
#include "critical_timing_monitor.h"
#include "deep_power_controller.h"
#include "fault_recorder.h"
#include "power_manager.h"
#include "supervisor_subsystem.h"
#include "watchdog_device.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define EXPECT_EQ(expected, actual) do { \
    int expected_value = (int)(expected); \
    int actual_value = (int)(actual); \
    if (expected_value != actual_value) { \
        printf("FAIL %s:%d expected %d actual %d\n", \
               __FILE__, __LINE__, expected_value, actual_value); \
        failures++; \
    } \
} while (0)

typedef struct {
    uint32_t timeout_ms;
    uint32_t remaining_ms;
    uint32_t start_calls;
    uint32_t refresh_calls;
    status_t refresh_status;
} fake_watchdog_t;

typedef struct {
    boot_metadata_t copies[2];
    uint8_t valid[2];
    uint32_t writes;
    status_t write_status;
} fake_metadata_store_t;

typedef struct {
    fault_record_t record;
    fault_injection_t last_injection;
    uint32_t clear_calls;
    uint32_t injection_calls;
    uint8_t present;
} fake_fault_store_t;

typedef struct {
    uint32_t stop_calls;
    uint32_t standby_calls;
    uint32_t elapsed_ms;
    power_wake_reason_t wake_reason;
    status_t stop_status;
    status_t standby_status;
} fake_deep_power_t;

static status_t fake_enter_stop(void *context, uint32_t requested_ms,
                                uint32_t *elapsed_ms,
                                power_wake_reason_t *wake_reason)
{
    fake_deep_power_t *fake = context;

    (void)requested_ms;
    fake->stop_calls++;
    *elapsed_ms = fake->elapsed_ms;
    *wake_reason = fake->wake_reason;
    return fake->stop_status;
}

static status_t fake_enter_standby(void *context)
{
    fake_deep_power_t *fake = context;

    fake->standby_calls++;
    return fake->standby_status;
}

static const deep_power_platform_ops_t fake_deep_power_ops = {
    fake_enter_stop,
    fake_enter_standby
};

static status_t fake_fault_load(void *context, fault_record_t *record)
{
    fake_fault_store_t *store = context;

    if (store->present == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    *record = store->record;
    return SYS_OK;
}

static status_t fake_fault_clear(void *context)
{
    fake_fault_store_t *store = context;

    memset(&store->record, 0, sizeof(store->record));
    store->present = 0u;
    store->clear_calls++;
    return SYS_OK;
}

static status_t fake_fault_inject(void *context,
                                  fault_injection_t injection)
{
    fake_fault_store_t *store = context;

    store->last_injection = injection;
    store->injection_calls++;
    return SYS_OK;
}

static const fault_recorder_ops_t fake_fault_ops = {
    fake_fault_load,
    fake_fault_clear,
    fake_fault_inject
};

static status_t fake_watchdog_start(void *context, uint32_t timeout_ms)
{
    fake_watchdog_t *watchdog = context;

    watchdog->timeout_ms = timeout_ms;
    watchdog->remaining_ms = timeout_ms;
    watchdog->start_calls++;
    return SYS_OK;
}

static status_t fake_watchdog_refresh(void *context)
{
    fake_watchdog_t *watchdog = context;

    watchdog->refresh_calls++;
    if (watchdog->refresh_status == SYS_OK) {
        watchdog->remaining_ms = watchdog->timeout_ms;
    }
    return watchdog->refresh_status;
}

static uint32_t fake_watchdog_remaining(const void *context)
{
    const fake_watchdog_t *watchdog = context;

    return watchdog->remaining_ms;
}

static const watchdog_device_ops_t fake_watchdog_ops = {
    fake_watchdog_start,
    fake_watchdog_refresh,
    fake_watchdog_remaining
};

static status_t fake_metadata_read(void *context, app_slot_t slot,
                                   boot_metadata_t *metadata)
{
    fake_metadata_store_t *store = context;
    unsigned int index = (unsigned int)slot;

    if (metadata == 0 || index > 1u || store->valid[index] == 0u) {
        return ERR_METADATA_INVALID;
    }
    *metadata = store->copies[index];
    return SYS_OK;
}

static status_t fake_metadata_write(void *context, app_slot_t slot,
                                    const boot_metadata_t *metadata)
{
    fake_metadata_store_t *store = context;
    unsigned int index = (unsigned int)slot;

    if (metadata == 0 || index > 1u ||
        boot_meta_validate(metadata) != SYS_OK) {
        return ERR_METADATA_INVALID;
    }
    if (store->write_status != SYS_OK) {
        return store->write_status;
    }
    store->copies[index] = *metadata;
    store->valid[index] = 1u;
    store->writes++;
    return SYS_OK;
}

static void test_watchdog_device(void)
{
    fake_watchdog_t fake = { 0 };
    watchdog_device_t watchdog;

    EXPECT_EQ(SYS_OK, watchdog_device_construct(
        &watchdog, &fake_watchdog_ops, &fake));
    EXPECT_EQ(ERR_DEVICE_NOT_READY, watchdog_device_refresh(&watchdog));
    EXPECT_EQ(SYS_OK, watchdog_device_start(&watchdog, 8000u));
    EXPECT_EQ(8000, watchdog_device_remaining_ms(&watchdog));
    EXPECT_EQ(SYS_OK, watchdog_device_refresh(&watchdog));
    EXPECT_EQ(1, watchdog.refresh_count);
    EXPECT_EQ(1, fake.start_calls);
    EXPECT_EQ(1, fake.refresh_calls);
}

static void test_fault_recorder(void)
{
    fake_fault_store_t store;
    fault_recorder_t recorder;
    fault_record_t record;
    fault_recorder_health_t health;

    memset(&store, 0, sizeof(store));
    EXPECT_EQ(SYS_OK, fault_recorder_construct(
        &recorder, &fake_fault_ops, &store, 1u));
    EXPECT_EQ(ERR_DEVICE_NOT_READY, fault_recorder_get(&recorder, &record));

    memset(&store.record, 0, sizeof(store.record));
    store.record.origin = FAULT_ORIGIN_HARDFAULT;
    store.record.sequence = 7u;
    store.record.stacked_pc = 0x08023456u;
    store.record.cfsr = 0x00008200u;
    EXPECT_EQ(SYS_OK, fault_record_finalize(&store.record));
    store.present = 1u;
    EXPECT_EQ(SYS_OK, fault_recorder_refresh(&recorder));
    EXPECT_EQ(SYS_OK, fault_recorder_get(&recorder, &record));
    EXPECT_EQ(7, record.sequence);
    EXPECT_EQ(FAULT_ORIGIN_HARDFAULT, record.origin);

    store.record.stacked_pc ^= 1u;
    EXPECT_EQ(ERR_CRC, fault_recorder_refresh(&recorder));
    EXPECT_EQ(ERR_DEVICE_NOT_READY, fault_recorder_get(&recorder, &record));
    EXPECT_EQ(SYS_OK, fault_recorder_get_health(&recorder, &health));
    EXPECT_EQ(1, health.invalid_records);

    EXPECT_EQ(ERR_INVALID_ARG, fault_recorder_inject(
        &recorder, FAULT_INJECTION_WATCHDOG, 0u));
    EXPECT_EQ(SYS_OK, fault_recorder_inject(
        &recorder, FAULT_INJECTION_WATCHDOG,
        FAULT_INJECTION_CONFIRMATION));
    EXPECT_EQ(1, store.injection_calls);
    EXPECT_EQ(FAULT_INJECTION_WATCHDOG, store.last_injection);
    EXPECT_EQ(SYS_OK, fault_recorder_clear(&recorder));
    EXPECT_EQ(1, store.clear_calls);
}

static void test_power_lock_and_sleep_gate(void)
{
    const power_manager_config_t config = {
        1000u, 5u, 500u, 2000u, 1UL << PM_LOCK_CAN_MONITORING
    };
    power_manager_t manager;
    power_manager_stats_t stats;
    uint32_t allowed = 0u;

    EXPECT_EQ(SYS_OK, power_manager_construct(&manager, &config, 0u));
    EXPECT_EQ(POWER_STANDBY_SHIPPING,
              power_manager_deepest_allowed(&manager));
    EXPECT_EQ(SYS_OK, power_manager_acquire(
        &manager, PM_LOCK_CAN_MONITORING, 10u));
    EXPECT_EQ(POWER_TICKLESS_SLEEP,
              power_manager_deepest_allowed(&manager));
    EXPECT_EQ(SYS_OK, power_manager_acquire(
        &manager, PM_LOCK_FLASH_WRITE, 20u));
    EXPECT_EQ(SYS_OK, power_manager_acquire(
        &manager, PM_LOCK_FLASH_WRITE, 25u));
    EXPECT_EQ(POWER_ACTIVE, power_manager_deepest_allowed(&manager));
    EXPECT_EQ(ERR_DEVICE_NOT_READY, power_manager_prepare_tickless(
        &manager, 100u, 4000u, &allowed));
    EXPECT_EQ(SYS_OK, power_manager_release(
        &manager, PM_LOCK_FLASH_WRITE, 30u));
    EXPECT_EQ(POWER_ACTIVE, power_manager_deepest_allowed(&manager));
    EXPECT_EQ(SYS_OK, power_manager_release(
        &manager, PM_LOCK_FLASH_WRITE, 40u));
    EXPECT_EQ(POWER_TICKLESS_SLEEP,
              power_manager_deepest_allowed(&manager));
    EXPECT_EQ(ERR_TIMEOUT, power_manager_prepare_tickless(
        &manager, 5000u, 4000u, &allowed));
    EXPECT_EQ(0, allowed);
    EXPECT_EQ(SYS_OK, power_manager_prepare_tickless(
        &manager, 3000u, 4000u, &allowed));
    EXPECT_EQ(3000, allowed);
    power_manager_record_wake(&manager, allowed, POWER_WAKE_SYSTICK);
    EXPECT_EQ(SYS_OK, power_manager_release(
        &manager, PM_LOCK_CAN_MONITORING, 3500u));
    EXPECT_EQ(ERR_INVALID_ARG, power_manager_release(
        &manager, PM_LOCK_CAN_MONITORING, 3501u));

    EXPECT_EQ(SYS_OK, power_manager_acquire(
        &manager, PM_LOCK_UI_ACTIVE, 3600u));
    EXPECT_EQ(POWER_ACTIVE, power_manager_deepest_allowed(&manager));
    EXPECT_EQ(POWER_ACTIVE, power_manager_evaluate(&manager, 5000u, 0u));
    EXPECT_EQ(SYS_OK, power_manager_release(
        &manager, PM_LOCK_UI_ACTIVE, 5001u));

    EXPECT_EQ(SYS_OK, power_manager_acquire(
        &manager, PM_LOCK_OTA, 5100u));
    EXPECT_EQ(POWER_ECO, power_manager_evaluate(&manager, 7500u, 0u));
    EXPECT_EQ(SYS_OK, power_manager_get_stats(&manager, &stats));
    EXPECT_EQ(1, stats.leak_events);
    EXPECT_EQ(1, stats.sleep_entries);
    EXPECT_EQ(3000, stats.cumulative_sleep_budget_ms);
    EXPECT_EQ(1, stats.release_errors);
}

static void test_deep_power_controller(void)
{
    const uint32_t required =
        DEEP_POWER_PARTICIPANT_ACQUISITION |
        DEEP_POWER_PARTICIPANT_STORAGE;
    deep_power_controller_config_t config = {
        1u, 1u, 1000u, 8000u, 500u, required, 5000u
    };
    fake_deep_power_t fake;
    deep_power_controller_t controller;
    deep_power_health_t health;

    memset(&fake, 0, sizeof(fake));
    fake.elapsed_ms = 1900u;
    fake.wake_reason = POWER_WAKE_INTERRUPT;
    fake.stop_status = SYS_OK;
    fake.standby_status = ERR_RESET_REQUIRED;
    EXPECT_EQ(SYS_OK, deep_power_controller_construct(
        &controller, &fake_deep_power_ops, &fake, &config));
    EXPECT_EQ(ERR_INVALID_ARG, deep_power_controller_request(
        &controller, POWER_STOP_PERIODIC, 2000u, 0u, 0u));
    EXPECT_EQ(SYS_OK, deep_power_controller_request(
        &controller, POWER_STOP_PERIODIC, 2000u,
        DEEP_POWER_CONFIRMATION, 0u));
    EXPECT_EQ(ERR_DEVICE_NOT_READY, deep_power_controller_process(
        &controller, DEEP_POWER_PARTICIPANT_ACQUISITION,
        POWER_STANDBY_SHIPPING, 6000u, 0u));
    EXPECT_EQ(ERR_DEVICE_NOT_READY, deep_power_controller_process(
        &controller, required, POWER_TICKLESS_SLEEP, 6000u, 0u));
    EXPECT_EQ(ERR_TIMEOUT, deep_power_controller_process(
        &controller, required, POWER_STANDBY_SHIPPING, 2400u, 0u));
    EXPECT_EQ(SYS_OK, deep_power_controller_process(
        &controller, required, POWER_STANDBY_SHIPPING, 6000u, 0u));
    EXPECT_EQ(SYS_OK, deep_power_controller_get_health(
        &controller, &health));
    EXPECT_EQ(1, fake.stop_calls);
    EXPECT_EQ(1, health.stop_entries);
    EXPECT_EQ(1, health.restore_count);
    EXPECT_EQ(1900, health.last_elapsed_ms);
    EXPECT_EQ(POWER_WAKE_INTERRUPT, health.last_wake_reason);
    EXPECT_EQ(0, health.request_pending);

    EXPECT_EQ(SYS_OK, deep_power_controller_request(
        &controller, POWER_STANDBY_SHIPPING, 0u,
        DEEP_POWER_CONFIRMATION, 0u));
    EXPECT_EQ(ERR_RESET_REQUIRED, deep_power_controller_process(
        &controller, required, POWER_STANDBY_SHIPPING, 6000u, 0u));
    EXPECT_EQ(SYS_OK, deep_power_controller_get_health(
        &controller, &health));
    EXPECT_EQ(1, fake.standby_calls);
    EXPECT_EQ(1, health.standby_entries);
    EXPECT_EQ(1, health.failures);
    EXPECT_EQ(0, health.request_pending);

    config.stop_enabled = 0u;
    config.standby_enabled = 0u;
    EXPECT_EQ(SYS_OK, deep_power_controller_construct(
        &controller, &fake_deep_power_ops, &fake, &config));
    EXPECT_EQ(ERR_UNSUPPORTED, deep_power_controller_request(
        &controller, POWER_STOP_PERIODIC, 2000u,
        DEEP_POWER_CONFIRMATION, 0u));
    EXPECT_EQ(ERR_UNSUPPORTED, deep_power_controller_request(
        &controller, POWER_STANDBY_SHIPPING, 0u,
        DEEP_POWER_CONFIRMATION, 0u));
}

static void test_critical_timing_monitor(void)
{
    critical_timing_monitor_t monitor;

    memset(&monitor, 0, sizeof(monitor));
    EXPECT_EQ(SYS_OK, critical_timing_monitor_enter(&monitor, 100u));
    EXPECT_EQ(SYS_OK, critical_timing_monitor_enter(&monitor, 120u));
    EXPECT_EQ(SYS_OK, critical_timing_monitor_exit(&monitor, 150u));
    EXPECT_EQ(SYS_OK, critical_timing_monitor_exit(&monitor, 220u));
    EXPECT_EQ(2, monitor.entries);
    EXPECT_EQ(2, monitor.exits);
    EXPECT_EQ(2, monitor.maximum_nesting);
    EXPECT_EQ(120, monitor.longest_cycles);
    EXPECT_EQ(ERR_INVALID_ARG,
              critical_timing_monitor_exit(&monitor, 230u));
    EXPECT_EQ(1, monitor.pairing_errors);

    memset(&monitor, 0, sizeof(monitor));
    EXPECT_EQ(SYS_OK, critical_timing_monitor_enter(
        &monitor, UINT32_MAX - 9u));
    EXPECT_EQ(SYS_OK, critical_timing_monitor_exit(&monitor, 10u));
    EXPECT_EQ(20, monitor.longest_cycles);
}

static supervisor_config_t make_supervisor_config(void)
{
    supervisor_config_t config;

    memset(&config, 0, sizeof(config));
    config.task_count = 3u;
    config.critical_task_mask = (1UL << 1) | (1UL << 2);
    config.task_timeout_ms[1] = 300u;
    config.task_timeout_ms[2] = 300u;
    config.startup_grace_ms = 500u;
    config.boot_confirm_stable_ms = 200u;
    return config;
}

static void test_supervisor_health_gate(void)
{
    fake_watchdog_t fake = { 0 };
    watchdog_device_t watchdog;
    supervisor_subsystem_t supervisor;
    supervisor_config_t config = make_supervisor_config();
    supervisor_health_t health;
    uint32_t heartbeat[3] = { 0u, 0u, 0u };

    EXPECT_EQ(SYS_OK, watchdog_device_construct(
        &watchdog, &fake_watchdog_ops, &fake));
    EXPECT_EQ(SYS_OK, watchdog_device_start(&watchdog, 1000u));
    EXPECT_EQ(SYS_OK, supervisor_subsystem_construct(
        &supervisor, &watchdog, &config));
    EXPECT_EQ(SYS_OK, supervisor_subsystem_start(
        &supervisor, 0u, heartbeat));
    EXPECT_EQ(ERR_DEVICE_NOT_READY, supervisor_subsystem_process(
        &supervisor, 50u, heartbeat, 1u));
    heartbeat[1]++;
    heartbeat[2]++;
    EXPECT_EQ(SYS_OK, supervisor_subsystem_process(
        &supervisor, 100u, heartbeat, 1u));
    EXPECT_EQ(1, fake.refresh_calls);
    EXPECT_EQ(0, supervisor_subsystem_boot_confirm_ready(
        &supervisor, 299u));
    EXPECT_EQ(1, supervisor_subsystem_boot_confirm_ready(
        &supervisor, 300u));
    EXPECT_EQ(ERR_TIMEOUT, supervisor_subsystem_process(
        &supervisor, 401u, heartbeat, 1u));
    EXPECT_EQ(1, fake.refresh_calls);
    EXPECT_EQ(SYS_OK, supervisor_subsystem_get_health(
        &supervisor, &health));
    EXPECT_EQ((1UL << 1) | (1UL << 2), health.stale_task_mask);
    EXPECT_EQ(0, health.healthy);

    heartbeat[1]++;
    heartbeat[2]++;
    fake.refresh_status = ERR_IO;
    EXPECT_EQ(ERR_IO, supervisor_subsystem_process(
        &supervisor, 450u, heartbeat, 1u));
    EXPECT_EQ(SYS_OK, supervisor_subsystem_get_health(
        &supervisor, &health));
    EXPECT_EQ(1, health.watchdog_failures);
    EXPECT_EQ(1, health.latched_faults);
}

static status_t prepare_trial(fake_metadata_store_t *store,
                              boot_meta_store_t *port)
{
    boot_metadata_t current;
    boot_metadata_t desired;
    boot_metadata_t committed;
    app_slot_t copy = SLOT_A;
    uint8_t digest[IMAGE_SHA256_LEN] = { 1u };
    status_t status;

    memset(store, 0, sizeof(*store));
    port->read = fake_metadata_read;
    port->write = fake_metadata_write;
    port->context = store;
    status = boot_meta_init_default(&current, SLOT_A, "1.0.0");
    if (status != SYS_OK) {
        return status;
    }
    store->copies[SLOT_A] = current;
    store->valid[SLOT_A] = 1u;
    status = boot_meta_request_staging(&current,
    &(const boot_staging_request_t){ SLOT_B, "2.0.0", 0x12345678u, digest, &desired });
    if (status == SYS_OK) {
        status = boot_meta_commit(port,
    &(const boot_meta_commit_request_t){ &current, copy, &desired, &committed, &copy });
    }
    if (status == SYS_OK) {
        status = boot_meta_request_trial(&committed, &desired);
    }
    if (status == SYS_OK) {
        status = boot_meta_commit(port,
    &(const boot_meta_commit_request_t){ &committed, copy, &desired, &committed, &copy });
    }
    return status;
}

static void test_boot_confirmation(void)
{
    fake_metadata_store_t store;
    boot_meta_store_t port;
    boot_confirmation_t confirmation;
    boot_confirmation_health_t health;
    uint32_t writes_before;

    EXPECT_EQ(SYS_OK, prepare_trial(&store, &port));
    writes_before = store.writes;
    EXPECT_EQ(SYS_OK, boot_confirmation_construct(
        &confirmation, &port, SLOT_B));
    EXPECT_EQ(SYS_OK, boot_confirmation_confirm(&confirmation));
    EXPECT_EQ(SYS_OK, boot_confirmation_get_health(
        &confirmation, &health));
    EXPECT_EQ(1, health.confirmed);
    EXPECT_EQ(SLOT_B, health.active_slot);
    EXPECT_EQ(SLOT_NONE, health.pending_slot);
    EXPECT_EQ(BOOT_STATE_NORMAL, health.boot_state);
    EXPECT_EQ(writes_before + 1u, store.writes);
    EXPECT_EQ(SYS_OK, boot_confirmation_confirm(&confirmation));
    EXPECT_EQ(writes_before + 1u, store.writes);

    EXPECT_EQ(SYS_OK, prepare_trial(&store, &port));
    EXPECT_EQ(SYS_OK, boot_confirmation_construct(
        &confirmation, &port, SLOT_A));
    EXPECT_EQ(ERR_SLOT_MISMATCH,
              boot_confirmation_confirm(&confirmation));

    EXPECT_EQ(SYS_OK, prepare_trial(&store, &port));
    store.write_status = ERR_FLASH_WRITE;
    EXPECT_EQ(SYS_OK, boot_confirmation_construct(
        &confirmation, &port, SLOT_B));
    EXPECT_EQ(ERR_FLASH_WRITE,
              boot_confirmation_confirm(&confirmation));
    EXPECT_EQ(ERR_FLASH_WRITE,
              boot_confirmation_confirm(&confirmation));
    EXPECT_EQ(SYS_OK, boot_confirmation_get_health(
        &confirmation, &health));
    EXPECT_EQ(0, health.confirmed);
}

int main(void)
{
    test_watchdog_device();
    test_fault_recorder();
    test_power_lock_and_sleep_gate();
    test_deep_power_controller();
    test_critical_timing_monitor();
    test_supervisor_health_gate();
    test_boot_confirmation();

    if (failures != 0) {
        printf("%d reliability/power test(s) failed\n", failures);
        return 1;
    }
    puts("reliability and power tests passed");
    return 0;
}
