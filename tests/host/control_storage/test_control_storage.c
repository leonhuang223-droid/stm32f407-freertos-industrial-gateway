#include "alarm_subsystem.h"
#include "app_context.h"
#include "external_flash_layout.h"
#include "relay.h"
#include "spi_bus.h"
#include "storage_media.h"
#include "storage_subsystem.h"
#include "w25q128.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECT_TRUE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
                #condition); \
        return 1; \
    } \
} while (0)

#define EXPECT_STATUS(expected, expression) do { \
    status_t actual_status = (expression); \
    if (actual_status != (expected)) { \
        fprintf(stderr, "FAIL %s:%d: expected %d, got %d\n", \
                __FILE__, __LINE__, (int)(expected), (int)actual_status); \
        return 1; \
    } \
} while (0)

typedef struct {
    int level;
    unsigned int init_calls;
    unsigned int write_calls;
    unsigned int suspend_calls;
    unsigned int resume_calls;
    int fail_init;
} fake_relay_port_t;

static status_t fake_relay_init(void *context, int inactive_level)
{
    fake_relay_port_t *port = context;

    port->level = inactive_level;
    port->init_calls++;
    return port->fail_init ? ERR_IO : SYS_OK;
}

static status_t fake_relay_write(void *context, int physical_level)
{
    fake_relay_port_t *port = context;

    port->level = physical_level;
    port->write_calls++;
    return SYS_OK;
}

static status_t fake_relay_read(void *context, int *physical_level)
{
    fake_relay_port_t *port = context;

    if (physical_level == 0) {
        return ERR_INVALID_ARG;
    }
    *physical_level = port->level;
    return SYS_OK;
}

static status_t fake_relay_suspend(void *context)
{
    fake_relay_port_t *port = context;

    port->suspend_calls++;
    return SYS_OK;
}

static status_t fake_relay_resume(void *context)
{
    fake_relay_port_t *port = context;

    port->resume_calls++;
    return SYS_OK;
}

static const relay_ops_t fake_relay_ops = {
    fake_relay_init,
    fake_relay_write,
    fake_relay_read,
    fake_relay_suspend,
    fake_relay_resume
};

static gateway_runtime_config_t default_config(void)
{
    gateway_runtime_config_t config;
    gateway_alarm_rule_config_t *rule;

    memset(&config, 0, sizeof(config));
    config.schema_version = GATEWAY_RUNTIME_CONFIG_SCHEMA_VERSION;
    config.revision = 1u;
    config.rule_count = 1u;
    rule = &config.rules[0];
    rule->point_id = GATEWAY_POINT_LOOP_CURRENT;
    rule->high_enabled = 1u;
    rule->low_enabled = 1u;
    rule->relay_on_alarm = 1u;
    rule->assert_samples = 3u;
    rule->recover_samples = 2u;
    rule->high_threshold = 1000;
    rule->low_threshold = 100;
    rule->hysteresis = 100;
    return config;
}

static gateway_measurement_t measurement(uint32_t sequence, int32_t value,
                                         gateway_quality_t quality)
{
    gateway_measurement_t sample;

    memset(&sample, 0, sizeof(sample));
    sample.point_id = GATEWAY_POINT_LOOP_CURRENT;
    sample.source = GATEWAY_SOURCE_ADS1115;
    sample.unit = GATEWAY_UNIT_MICROAMP;
    sample.sequence = sequence;
    sample.monotonic_ms = sequence * 10u;
    sample.wall_time_ms = 1700000000000ULL + sequence * 10u;
    sample.raw_value = value;
    sample.engineering_value = value;
    sample.quality = quality;
    sample.error = quality == GATEWAY_QUALITY_GOOD ? SYS_OK : ERR_IO;
    return sample;
}

static int test_alarm_relay_loop(void)
{
    fake_relay_port_t port;
    relay_config_t relay_config = { 1u, RELAY_DEENERGIZED };
    relay_t relay;
    alarm_subsystem_t alarm;
    gateway_runtime_config_t config = default_config();
    gateway_alarm_event_t events[ALARM_MAX_EVENTS_PER_MEASUREMENT];
    gateway_measurement_t sample;
    relay_state_t state;
    size_t event_count;
    uint32_t sequence = 1u;
    unsigned int i;

    memset(&port, 0, sizeof(port));
    EXPECT_STATUS(SYS_OK, relay_construct(&relay, &fake_relay_ops, &port,
                                          &relay_config));
    EXPECT_STATUS(SYS_OK, alarm_subsystem_construct(&alarm, &relay, &config));
    EXPECT_STATUS(SYS_OK, alarm_subsystem_start(&alarm));
    EXPECT_TRUE(port.level == 0 && port.init_calls == 1u);

    for (i = 0u; i < 2u; ++i) {
        sample = measurement(sequence++, 1100, GATEWAY_QUALITY_GOOD);
        EXPECT_STATUS(SYS_OK, alarm_subsystem_process(
            &alarm, &sample, events, ALARM_MAX_EVENTS_PER_MEASUREMENT,
            &event_count));
        EXPECT_TRUE(event_count == 0u);
    }
    sample = measurement(sequence++, 1100, GATEWAY_QUALITY_GOOD);
    EXPECT_STATUS(SYS_OK, alarm_subsystem_process(
        &alarm, &sample, events, ALARM_MAX_EVENTS_PER_MEASUREMENT,
        &event_count));
    EXPECT_TRUE(event_count == 1u);
    EXPECT_TRUE(events[0].type == GATEWAY_ALARM_HIGH);
    EXPECT_TRUE(events[0].transition == GATEWAY_ALARM_ENTERED);
    EXPECT_STATUS(SYS_OK, relay_get_state(&relay, &state));
    EXPECT_TRUE(state == RELAY_ENERGIZED && port.level == 1);

    sample = measurement(sequence++, 950, GATEWAY_QUALITY_GOOD);
    EXPECT_STATUS(SYS_OK, alarm_subsystem_process(
        &alarm, &sample, events, ALARM_MAX_EVENTS_PER_MEASUREMENT,
        &event_count));
    EXPECT_TRUE(event_count == 0u);
    EXPECT_TRUE(alarm_subsystem_active_count(&alarm) == 1u);
    for (i = 0u; i < 2u; ++i) {
        sample = measurement(sequence++, 900, GATEWAY_QUALITY_GOOD);
        EXPECT_STATUS(SYS_OK, alarm_subsystem_process(
            &alarm, &sample, events, ALARM_MAX_EVENTS_PER_MEASUREMENT,
            &event_count));
    }
    EXPECT_TRUE(event_count == 1u);
    EXPECT_TRUE(events[0].transition == GATEWAY_ALARM_RECOVERED);
    EXPECT_STATUS(SYS_OK, relay_get_state(&relay, &state));
    EXPECT_TRUE(state == RELAY_DEENERGIZED && port.level == 0);

    for (i = 0u; i < 3u; ++i) {
        sample = measurement(sequence++, 1100, GATEWAY_QUALITY_GOOD);
        EXPECT_STATUS(SYS_OK, alarm_subsystem_process(
            &alarm, &sample, events, ALARM_MAX_EVENTS_PER_MEASUREMENT,
            &event_count));
    }
    EXPECT_STATUS(SYS_OK, relay_get_state(&relay, &state));
    EXPECT_TRUE(state == RELAY_ENERGIZED);
    sample = measurement(sequence++, 1100, GATEWAY_QUALITY_COMM_ERROR);
    EXPECT_STATUS(SYS_OK, alarm_subsystem_process(
        &alarm, &sample, events, ALARM_MAX_EVENTS_PER_MEASUREMENT,
        &event_count));
    EXPECT_STATUS(SYS_OK, relay_get_state(&relay, &state));
    EXPECT_TRUE(state == RELAY_DEENERGIZED);
    return 0;
}

typedef struct {
    uint8_t bytes[8192];
    uint32_t jedec_id;
    uint32_t program_commands;
    uint32_t erase_commands;
    uint32_t delays;
    int selected;
    int write_enabled;
    int powered_down;
} fake_spi_flash_t;

static status_t fake_spi_select(void *context, int active)
{
    fake_spi_flash_t *flash = context;

    flash->selected = active;
    return SYS_OK;
}

static uint32_t command_address(const uint8_t *tx)
{
    return ((uint32_t)tx[1] << 16u) |
           ((uint32_t)tx[2] << 8u) | tx[3];
}

static status_t fake_spi_transfer(void *context, const uint8_t *tx,
                                  uint8_t *rx, size_t length)
{
    fake_spi_flash_t *flash = context;
    uint32_t address;
    size_t i;

    if (!flash->selected || tx == 0 || length == 0u) {
        return ERR_IO;
    }
    if (rx != 0) {
        memset(rx, 0, length);
    }
    if (flash->powered_down && tx[0] != 0xabu) {
        return ERR_DEVICE_NOT_READY;
    }
    switch (tx[0]) {
    case 0xabu:
        flash->powered_down = 0;
        return SYS_OK;
    case 0x9fu:
        if (rx == 0 || length != 4u) {
            return ERR_PROTOCOL;
        }
        rx[1] = (uint8_t)(flash->jedec_id >> 16u);
        rx[2] = (uint8_t)(flash->jedec_id >> 8u);
        rx[3] = (uint8_t)flash->jedec_id;
        return SYS_OK;
    case 0x05u:
        if (rx == 0 || length != 2u) {
            return ERR_PROTOCOL;
        }
        rx[1] = flash->write_enabled ? 0x02u : 0u;
        return SYS_OK;
    case 0x06u:
        flash->write_enabled = 1;
        return SYS_OK;
    case 0x03u:
        if (rx == 0 || length < 5u) {
            return ERR_PROTOCOL;
        }
        address = command_address(tx);
        if (address + length - 4u > sizeof(flash->bytes)) {
            return ERR_INVALID_ARG;
        }
        memcpy(&rx[4], &flash->bytes[address], length - 4u);
        return SYS_OK;
    case 0x02u:
        if (!flash->write_enabled || length < 5u) {
            return ERR_FLASH_WRITE;
        }
        address = command_address(tx);
        if (address + length - 4u > sizeof(flash->bytes)) {
            return ERR_INVALID_ARG;
        }
        for (i = 4u; i < length; ++i) {
            flash->bytes[address + i - 4u] &= tx[i];
        }
        flash->write_enabled = 0;
        flash->program_commands++;
        return SYS_OK;
    case 0x20u:
        if (!flash->write_enabled || length != 4u) {
            return ERR_FLASH_ERASE;
        }
        address = command_address(tx);
        if (address % EXTERNAL_FLASH_SECTOR_SIZE != 0u ||
            address + EXTERNAL_FLASH_SECTOR_SIZE > sizeof(flash->bytes)) {
            return ERR_INVALID_ARG;
        }
        memset(&flash->bytes[address], 0xff, EXTERNAL_FLASH_SECTOR_SIZE);
        flash->write_enabled = 0;
        flash->erase_commands++;
        return SYS_OK;
    case 0xb9u:
        flash->powered_down = 1;
        return SYS_OK;
    default:
        return ERR_UNSUPPORTED;
    }
}

static void fake_spi_delay(void *context, uint32_t delay_ms)
{
    fake_spi_flash_t *flash = context;

    flash->delays += delay_ms;
}

static const spi_device_ops_t fake_spi_ops = {
    fake_spi_select, fake_spi_transfer, fake_spi_delay
};

static int test_w25q128_commands(void)
{
    fake_spi_flash_t flash;
    spi_device_t spi;
    w25q128_t device;
    w25q128_config_t config = { 0xEF4018u, sizeof(flash.bytes), 10u };
    uint8_t source[20];
    uint8_t result[20];
    unsigned int i;

    memset(&flash, 0, sizeof(flash));
    memset(flash.bytes, 0xff, sizeof(flash.bytes));
    flash.jedec_id = config.expected_jedec_id;
    flash.powered_down = 1;
    for (i = 0u; i < sizeof(source); ++i) {
        source[i] = (uint8_t)(0xa0u + i);
    }
    EXPECT_STATUS(SYS_OK, spi_device_construct(&spi, &fake_spi_ops,
                                                &flash, 10u));
    EXPECT_STATUS(SYS_OK, w25q128_construct(&device, &spi, &config));
    EXPECT_STATUS(SYS_OK, w25q128_init(&device));
    EXPECT_TRUE(device.health.detected_jedec_id == 0xEF4018u);
    EXPECT_STATUS(SYS_OK, w25q128_program(&device, 250u, source,
                                          sizeof(source)));
    EXPECT_TRUE(flash.program_commands == 2u);
    EXPECT_STATUS(SYS_OK, w25q128_read(&device, 250u, result,
                                       sizeof(result)));
    EXPECT_TRUE(memcmp(source, result, sizeof(source)) == 0);
    EXPECT_STATUS(SYS_OK, w25q128_erase_sector(&device, 0u));
    EXPECT_TRUE(flash.erase_commands == 1u && flash.bytes[250] == 0xffu);
    EXPECT_STATUS(SYS_OK, w25q128_power_down(&device));
    EXPECT_TRUE(flash.powered_down != 0);
    EXPECT_STATUS(SYS_OK, w25q128_wake(&device));
    EXPECT_TRUE(flash.powered_down == 0);
    return 0;
}

static int test_control_startup_gate(void)
{
    app_context_t context;
    app_control_storage_config_t config;
    fake_spi_flash_t flash;
    fake_relay_port_t relay_port;

    memset(&flash, 0, sizeof(flash));
    memset(flash.bytes, 0xff, sizeof(flash.bytes));
    flash.jedec_id = EXTERNAL_FLASH_JEDEC_ID;
    memset(&relay_port, 0, sizeof(relay_port));
    relay_port.fail_init = 1;
    memset(&config, 0, sizeof(config));
    config.storage_spi_ops = &fake_spi_ops;
    config.storage_spi_context = &flash;
    config.storage_spi_timeout_ms = 10u;
    config.w25q128.expected_jedec_id = EXTERNAL_FLASH_JEDEC_ID;
    config.w25q128.total_size = EXTERNAL_FLASH_TOTAL_SIZE;
    config.w25q128.operation_timeout_ms = 10u;
    config.relay_ops = &fake_relay_ops;
    config.relay_context = &relay_port;
    config.relay.active_high = 1u;
    config.relay.safe_state = RELAY_DEENERGIZED;
    config.default_runtime_config = default_config();

    EXPECT_STATUS(SYS_OK, app_context_init(&context));
    EXPECT_STATUS(ERR_IO, app_context_configure_control_storage(&context,
                                                                 &config));
    EXPECT_TRUE((context.initialization_mask & APP_INITIALIZED_STORAGE) != 0u);
    EXPECT_TRUE((context.initialization_mask & APP_INITIALIZED_CONTROL) == 0u);
    EXPECT_TRUE(context.storage_startup_status != SYS_OK);
    EXPECT_TRUE(context.control_startup_status == ERR_IO);
    return 0;
}

typedef struct {
    uint8_t *bytes;
    size_t size;
    uint32_t erase_count;
    uint32_t program_count;
    int powered_down;
} fake_storage_t;

static int storage_range_valid(const fake_storage_t *storage,
                               uint32_t address, size_t length)
{
    return storage != 0 && length != 0u && address < storage->size &&
           length <= storage->size - address;
}

static status_t fake_storage_read(void *context, uint32_t address,
                                  uint8_t *buffer, size_t length)
{
    fake_storage_t *storage = context;

    if (buffer == 0 || !storage_range_valid(storage, address, length)) {
        return ERR_INVALID_ARG;
    }
    if (storage->powered_down) {
        return ERR_DEVICE_NOT_READY;
    }
    memcpy(buffer, &storage->bytes[address], length);
    return SYS_OK;
}

static status_t fake_storage_program(void *context, uint32_t address,
                                     const uint8_t *data, size_t length)
{
    fake_storage_t *storage = context;
    size_t i;

    if (data == 0 || !storage_range_valid(storage, address, length)) {
        return ERR_INVALID_ARG;
    }
    if (storage->powered_down) {
        return ERR_DEVICE_NOT_READY;
    }
    for (i = 0u; i < length; ++i) {
        if ((storage->bytes[address + i] & data[i]) != data[i]) {
            return ERR_FLASH_WRITE;
        }
    }
    for (i = 0u; i < length; ++i) {
        storage->bytes[address + i] &= data[i];
    }
    storage->program_count++;
    return SYS_OK;
}

static status_t fake_storage_erase(void *context, uint32_t address)
{
    fake_storage_t *storage = context;

    if (address % EXTERNAL_FLASH_SECTOR_SIZE != 0u ||
        !storage_range_valid(storage, address, EXTERNAL_FLASH_SECTOR_SIZE)) {
        return ERR_INVALID_ARG;
    }
    if (storage->powered_down) {
        return ERR_DEVICE_NOT_READY;
    }
    memset(&storage->bytes[address], 0xff, EXTERNAL_FLASH_SECTOR_SIZE);
    storage->erase_count++;
    return SYS_OK;
}

static status_t fake_storage_wake(void *context)
{
    fake_storage_t *storage = context;

    storage->powered_down = 0;
    return SYS_OK;
}

static status_t fake_storage_power_down(void *context)
{
    fake_storage_t *storage = context;

    storage->powered_down = 1;
    return SYS_OK;
}

static int test_storage_persistence(void)
{
    static const storage_media_ops_t media_ops = {
        fake_storage_read,
        fake_storage_program,
        fake_storage_erase,
        fake_storage_wake,
        fake_storage_power_down
    };
    fake_storage_t fake;
    storage_media_t media;
    storage_subsystem_t storage;
    storage_subsystem_t remounted;
    storage_subsystem_t fallback;
    storage_subsystem_t crash_rotated;
    storage_subsystem_t crash_recovered;
    gateway_runtime_config_t config = default_config();
    gateway_runtime_config_t loaded;
    gateway_storage_log_request_t log_request;
    gateway_storage_alarm_request_t alarm_request;
    gateway_storage_config_request_t config_request;
    fault_record_t fault;
    fault_record_t loaded_fault;
    storage_health_t health;
    const external_flash_partition_t *active_partition;
    uint32_t program_count;
    unsigned int i;
    int result = 1;

    memset(&fake, 0, sizeof(fake));
    fake.size = EXTERNAL_FLASH_TOTAL_SIZE;
    fake.bytes = malloc(fake.size);
    if (fake.bytes == 0) {
        fprintf(stderr, "FAIL: unable to allocate fake W25Q128\n");
        return 1;
    }
    memset(fake.bytes, 0xff, fake.size);
    EXPECT_STATUS(SYS_OK, external_flash_layout_validate());
    EXPECT_STATUS(SYS_OK, storage_media_construct(
        &media, &media_ops, &fake, EXTERNAL_FLASH_TOTAL_SIZE,
        EXTERNAL_FLASH_PAGE_SIZE, EXTERNAL_FLASH_SECTOR_SIZE));
    EXPECT_STATUS(SYS_OK, storage_subsystem_construct(&storage, &media,
                                                       &config));
    EXPECT_STATUS(SYS_OK, storage_subsystem_start(&storage));
    EXPECT_STATUS(SYS_OK, storage_subsystem_load_config(&storage, &loaded));
    EXPECT_TRUE(loaded.revision == 1u);

    memset(&fault, 0, sizeof(fault));
    fault.origin = FAULT_ORIGIN_HARDFAULT;
    fault.sequence = 42u;
    fault.reset_flags = 0x04000000u;
    fault.task_token = 0x20001234u;
    fault.stacked_lr = 0x08012345u;
    fault.stacked_pc = 0x08023456u;
    fault.cfsr = 0x00008200u;
    EXPECT_STATUS(SYS_OK, fault_record_finalize(&fault));
    EXPECT_STATUS(SYS_OK, storage_subsystem_archive_fault(&storage, &fault));
    EXPECT_STATUS(SYS_OK, storage_subsystem_load_latest_fault(
        &storage, &loaded_fault));
    EXPECT_TRUE(loaded_fault.sequence == 42u &&
                loaded_fault.stacked_pc == 0x08023456u);
    program_count = fake.program_count;
    EXPECT_STATUS(SYS_OK, storage_subsystem_archive_fault(&storage, &fault));
    EXPECT_TRUE(fake.program_count == program_count);
    EXPECT_STATUS(SYS_OK, storage_subsystem_get_health(&storage, &health));
    EXPECT_TRUE(health.crash_archives == 1u &&
                health.crash_duplicates == 1u &&
                health.latest_crash_valid == 1u &&
                health.latest_crash_sequence == 42u);

    memset(&log_request, 0, sizeof(log_request));
    log_request.sequence = 1u;
    log_request.measurement = measurement(1u, 1234, GATEWAY_QUALITY_GOOD);
    EXPECT_STATUS(SYS_OK, storage_subsystem_append_log(&storage,
                                                       &log_request));
    memset(&alarm_request, 0, sizeof(alarm_request));
    alarm_request.event.event_id = 1u;
    alarm_request.event.measurement_sequence = 1u;
    alarm_request.event.point_id = GATEWAY_POINT_LOOP_CURRENT;
    alarm_request.event.type = GATEWAY_ALARM_HIGH;
    alarm_request.event.transition = GATEWAY_ALARM_ENTERED;
    alarm_request.event.quality = GATEWAY_QUALITY_GOOD;
    alarm_request.event.threshold = 1000;
    alarm_request.event.value = 1234;
    EXPECT_STATUS(SYS_OK, storage_subsystem_append_alarm(&storage,
                                                         &alarm_request));

    memset(&config_request, 0, sizeof(config_request));
    config_request.request_id = 7u;
    config_request.config = config;
    config_request.config.revision = 2u;
    config_request.config.rules[0].high_threshold = 1200;
    EXPECT_STATUS(SYS_OK, storage_subsystem_save_config(&storage,
                                                        &config_request));
    EXPECT_STATUS(SYS_OK, storage_subsystem_construct(&remounted, &media,
                                                       &config));
    EXPECT_STATUS(SYS_OK, storage_subsystem_start(&remounted));
    EXPECT_STATUS(SYS_OK, storage_subsystem_load_config(&remounted, &loaded));
    EXPECT_TRUE(loaded.revision == 2u &&
                loaded.rules[0].high_threshold == 1200);
    EXPECT_TRUE(remounted.runtime_log.next_slot == 2u);
    EXPECT_TRUE(remounted.alarm_log.next_slot == 2u);
    EXPECT_TRUE(remounted.crash_log.next_slot == 2u);
    EXPECT_STATUS(SYS_OK, storage_subsystem_load_latest_fault(
        &remounted, &loaded_fault));
    EXPECT_TRUE(loaded_fault.sequence == fault.sequence &&
                loaded_fault.crc32 == fault.crc32);
    program_count = fake.program_count;
    EXPECT_STATUS(SYS_OK, storage_subsystem_archive_fault(&remounted, &fault));
    EXPECT_TRUE(fake.program_count == program_count);
    fault.sequence++;
    fault.origin = FAULT_ORIGIN_BUSFAULT;
    fault.stacked_pc++;
    EXPECT_STATUS(SYS_OK, fault_record_finalize(&fault));
    EXPECT_STATUS(SYS_OK, storage_subsystem_archive_fault(&remounted, &fault));
    EXPECT_STATUS(SYS_OK, storage_subsystem_load_latest_fault(
        &remounted, &loaded_fault));
    EXPECT_TRUE(loaded_fault.sequence == 43u &&
                loaded_fault.origin == FAULT_ORIGIN_BUSFAULT);
    for (i = 0u; i < 29u; ++i) {
        fault.sequence++;
        fault.stacked_pc++;
        EXPECT_STATUS(SYS_OK, fault_record_finalize(&fault));
        EXPECT_STATUS(SYS_OK, storage_subsystem_archive_fault(
            &remounted, &fault));
    }
    EXPECT_TRUE(remounted.crash_log.next_slot == 32u);
    fault.sequence++;
    fault.stacked_pc++;
    EXPECT_STATUS(SYS_OK, fault_record_finalize(&fault));
    EXPECT_STATUS(SYS_OK, storage_subsystem_archive_fault(&remounted,
                                                           &fault));
    EXPECT_TRUE(remounted.crash_log.generation == 2u &&
                remounted.crash_log.active_sector == 1u &&
                remounted.crash_log.next_slot == 2u);
    EXPECT_STATUS(SYS_OK, storage_subsystem_construct(
        &crash_rotated, &media, &config));
    EXPECT_STATUS(SYS_OK, storage_subsystem_start(&crash_rotated));
    EXPECT_STATUS(SYS_OK, storage_subsystem_load_latest_fault(
        &crash_rotated, &loaded_fault));
    EXPECT_TRUE(loaded_fault.sequence == fault.sequence);
    fake.bytes[EXTERNAL_FLASH_CRASH_START + EXTERNAL_FLASH_SECTOR_SIZE +
               STORAGE_RECORD_SIZE + 32u] ^= 0x01u;
    EXPECT_STATUS(SYS_OK, storage_subsystem_construct(
        &crash_recovered, &media, &config));
    EXPECT_STATUS(SYS_OK, storage_subsystem_start(&crash_recovered));
    EXPECT_STATUS(SYS_OK, storage_subsystem_load_latest_fault(
        &crash_recovered, &loaded_fault));
    EXPECT_TRUE(loaded_fault.sequence == fault.sequence - 1u);

    EXPECT_STATUS(SYS_OK, storage_subsystem_get_health(&remounted, &health));
    active_partition = external_flash_partition_get(
        health.active_config_copy == 0u
            ? EXTERNAL_FLASH_PARTITION_CONFIG_A
            : EXTERNAL_FLASH_PARTITION_CONFIG_B);
    fake.bytes[active_partition->start + 32u] ^= 0x01u;
    EXPECT_STATUS(SYS_OK, storage_subsystem_construct(&fallback, &media,
                                                       &config));
    EXPECT_STATUS(SYS_OK, storage_subsystem_start(&fallback));
    EXPECT_STATUS(SYS_OK, storage_subsystem_load_config(&fallback, &loaded));
    EXPECT_TRUE(loaded.revision == 1u);
    EXPECT_STATUS(SYS_OK, storage_subsystem_get_health(&fallback, &health));
    EXPECT_TRUE(health.mount_invalid_records >= 1u);

    for (i = 0u; i < 31u; ++i) {
        log_request.sequence++;
        log_request.measurement = measurement(log_request.sequence,
                                              1300 + (int32_t)i,
                                              GATEWAY_QUALITY_GOOD);
        EXPECT_STATUS(SYS_OK, storage_subsystem_append_log(&fallback,
                                                           &log_request));
    }
    EXPECT_TRUE(fallback.runtime_log.generation == 2u);
    EXPECT_TRUE(fallback.runtime_log.active_sector == 1u);
    EXPECT_TRUE(fallback.runtime_log.next_slot == 2u);
    EXPECT_STATUS(SYS_OK, storage_subsystem_power_down(&fallback));
    EXPECT_TRUE(fake.powered_down != 0);
    EXPECT_STATUS(SYS_OK, storage_subsystem_power_down(&fallback));
    EXPECT_STATUS(SYS_OK, storage_subsystem_wake(&fallback));
    EXPECT_TRUE(fake.powered_down == 0);
    EXPECT_STATUS(SYS_OK, storage_subsystem_wake(&fallback));
    EXPECT_STATUS(SYS_OK, storage_subsystem_get_health(&fallback, &health));
    EXPECT_TRUE(health.power_downs == 1u && health.wakeups == 1u &&
                health.power_failures == 0u && health.powered_down == 0u);
    result = 0;
    free(fake.bytes);
    return result;
}

int main(void)
{
    if (test_alarm_relay_loop() != 0 ||
        test_w25q128_commands() != 0 ||
        test_control_startup_gate() != 0 ||
        test_storage_persistence() != 0) {
        return 1;
    }
    puts("control_storage.host: PASS");
    return 0;
}
