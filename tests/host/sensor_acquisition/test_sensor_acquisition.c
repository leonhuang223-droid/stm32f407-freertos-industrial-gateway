#include "app_context.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define EXPECT_EQ(expected, actual) do { \
    long expected_value = (long)(expected); \
    long actual_value = (long)(actual); \
    if (expected_value != actual_value) { \
        printf("FAIL %s:%d expected %ld actual %ld\n", \
               __FILE__, __LINE__, expected_value, actual_value); \
        failures++; \
    } \
} while (0)

typedef struct {
    uint16_t ads_config;
    int16_t ads_conversion;
    uint16_t sht_temperature;
    uint16_t sht_humidity;
    uint16_t last_sht_command;
    uint32_t delayed_ms;
    uint8_t fail_ads_conversion;
    uint8_t fail_ads_self_test;
} fake_i2c_t;

typedef struct {
    uint8_t config;
    uint8_t fault;
    uint16_t rtd_code;
    uint32_t delayed_ms;
    unsigned int selected;
} fake_spi_t;

static status_t fake_i2c_write(void *context, uint8_t address,
                               const uint8_t *data, size_t length)
{
    fake_i2c_t *fake = context;

    if (address == 0x48u && length == 3u && data[0] == 0x01u) {
        fake->ads_config = ((uint16_t)data[1] << 8u) | data[2];
        return SYS_OK;
    }
    if (address == 0x44u && length == 2u) {
        fake->last_sht_command = ((uint16_t)data[0] << 8u) | data[1];
        return SYS_OK;
    }
    return ERR_INVALID_ARG;
}

static status_t fake_i2c_read(void *context, uint8_t address,
                              uint8_t *data, size_t length)
{
    fake_i2c_t *fake = context;

    if (address != 0x44u || length != 6u) {
        return ERR_INVALID_ARG;
    }
    data[0] = (uint8_t)(fake->sht_temperature >> 8u);
    data[1] = (uint8_t)fake->sht_temperature;
    data[2] = sht30_crc8(data, 2u);
    data[3] = (uint8_t)(fake->sht_humidity >> 8u);
    data[4] = (uint8_t)fake->sht_humidity;
    data[5] = sht30_crc8(&data[3], 2u);
    return SYS_OK;
}

static status_t fake_i2c_write_read(void *context,
    uint8_t address,
    const i2c_transfer_t *parameters)
{
    if (parameters == 0) {
        return ERR_INVALID_ARG;
    }
    const uint8_t *write_data = parameters->write_data;
    size_t write_length = parameters->write_length;
    uint8_t *read_data = parameters->read_data;
    size_t read_length = parameters->read_length;

    fake_i2c_t *fake = context;

    if (address == 0x48u && write_length == 1u && read_length == 2u) {
        uint16_t value;

        if (write_data[0] == 0x01u && fake->fail_ads_self_test != 0u) {
            return ERR_IO;
        }
        if (write_data[0] == 0x00u && fake->fail_ads_conversion != 0u) {
            return ERR_TIMEOUT;
        }
        value = write_data[0] == 0x00u
            ? (uint16_t)fake->ads_conversion
            : (uint16_t)(fake->ads_config | 0x8000u);
        read_data[0] = (uint8_t)(value >> 8u);
        read_data[1] = (uint8_t)value;
        return SYS_OK;
    }
    if (address == 0x44u && write_length == 2u && read_length == 3u &&
        write_data[0] == 0xF3u && write_data[1] == 0x2Du) {
        read_data[0] = 0u;
        read_data[1] = 0u;
        read_data[2] = sht30_crc8(read_data, 2u);
        return SYS_OK;
    }
    return ERR_INVALID_ARG;
}

static void fake_i2c_delay(void *context, uint32_t delay_ms)
{
    ((fake_i2c_t *)context)->delayed_ms += delay_ms;
}

static status_t fake_spi_select(void *context, int active)
{
    fake_spi_t *fake = context;
    fake->selected = active != 0 ? 1u : 0u;
    return SYS_OK;
}

static status_t fake_spi_transfer(void *context, const uint8_t *tx,
                                  uint8_t *rx, size_t length)
{
    fake_spi_t *fake = context;
    uint8_t reg = tx[0] & 0x7Fu;

    if (fake->selected == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    if ((tx[0] & 0x80u) != 0u && length == 2u) {
        if (reg == 0u) {
            fake->config = tx[1];
        }
        return SYS_OK;
    }
    if (rx == 0) {
        return ERR_INVALID_ARG;
    }
    memset(rx, 0, length);
    if (reg == 0u && length == 2u) {
        rx[1] = fake->config;
        return SYS_OK;
    }
    if (reg == 1u && length == 3u) {
        uint16_t value = (uint16_t)((fake->rtd_code << 1u) |
                                    (fake->fault != 0u ? 1u : 0u));
        rx[1] = (uint8_t)(value >> 8u);
        rx[2] = (uint8_t)value;
        return SYS_OK;
    }
    if (reg == 7u && length == 2u) {
        rx[1] = fake->fault;
        return SYS_OK;
    }
    return ERR_INVALID_ARG;
}

static void fake_spi_delay(void *context, uint32_t delay_ms)
{
    ((fake_spi_t *)context)->delayed_ms += delay_ms;
}

static const i2c_bus_ops_t i2c_ops = {
    fake_i2c_write, fake_i2c_read, fake_i2c_write_read, fake_i2c_delay
};

static const spi_device_ops_t spi_ops = {
    fake_spi_select, fake_spi_transfer, fake_spi_delay
};

static app_acquisition_config_t make_config(fake_i2c_t *i2c,
                                            fake_spi_t *spi)
{
    app_acquisition_config_t config;

    memset(&config, 0, sizeof(config));
    config.i2c_ops = &i2c_ops;
    config.i2c_context = i2c;
    config.i2c_timeout_ms = 20u;
    config.max31865_spi_ops = &spi_ops;
    config.max31865_spi_context = spi;
    config.spi_timeout_ms = 20u;
    config.ads1115.address = 0x48u;
    config.ads1115.channel = 0u;
    config.ads1115.full_scale = ADS1115_FSR_4096_MV;
    config.ads1115.data_rate = ADS1115_SPS_860;
    config.ads1115.shunt_ohms = 100u;
    config.ads1115.valid_min_microamp = 4000;
    config.ads1115.valid_max_microamp = 20000;
    config.ads1115.point_id = GATEWAY_POINT_LOOP_CURRENT;
    config.max31865.reference_resistor_milliohm = 400000u;
    config.max31865.rtd_nominal_milliohm = 100000u;
    config.max31865.point_id = GATEWAY_POINT_PT100_TEMPERATURE;
    config.max31865.bias_settle_ms = 10u;
    config.max31865.three_wire = 1u;
    config.max31865.filter_50hz = 1u;
    config.sht30.address = 0x44u;
    config.sht30.repeatability = SHT30_REPEATABILITY_HIGH;
    config.sht30.temperature_point_id = GATEWAY_POINT_AMBIENT_TEMPERATURE;
    config.sht30.humidity_point_id = GATEWAY_POINT_RELATIVE_HUMIDITY;
    config.schedule.ads1115_period_ms = 100u;
    config.schedule.max31865_period_ms = 500u;
    config.schedule.sht30_period_ms = 1000u;
    return config;
}

static void test_conversion_helpers(void)
{
    int32_t temperature = -999999;
    static const uint8_t crc_sample[2] = { 0xBEu, 0xEFu };

    EXPECT_EQ(0x92, sht30_crc8(crc_sample, sizeof(crc_sample)));
    EXPECT_EQ(SYS_OK, max31865_temperature_millicelsius(
        8192u, 400000u, 100000u, &temperature));
    EXPECT_EQ(0, temperature);
}

static void test_complete_acquisition_cycle(void)
{
    fake_i2c_t i2c = { 0u, 16000, 0x6666u, 0x8000u, 0u, 0u, 0u, 0u };
    fake_spi_t spi = { 0u, 0u, 8192u, 0u, 0u };
    app_acquisition_config_t config = make_config(&i2c, &spi);
    app_context_t context;
    gateway_measurement_t measurements[ACQUISITION_MEASUREMENT_COUNT];
    size_t count = 0u;

    EXPECT_EQ(SYS_OK, app_context_init(&context));
    EXPECT_EQ(SYS_OK, app_context_configure_acquisition(&context, &config));
    EXPECT_EQ(APP_INITIALIZED_ACQUISITION,
              context.initialization_mask & APP_INITIALIZED_ACQUISITION);
    EXPECT_EQ(SYS_OK, acquisition_subsystem_process(
        &context.acquisition, 1234u, measurements,
        ACQUISITION_MEASUREMENT_COUNT, &count));
    EXPECT_EQ(4, count);
    EXPECT_EQ(20000, measurements[0].engineering_value);
    EXPECT_EQ(GATEWAY_QUALITY_GOOD, measurements[0].quality);
    EXPECT_EQ(0, measurements[1].engineering_value);
    EXPECT_EQ(25000, measurements[2].engineering_value);
    EXPECT_EQ(50000, measurements[3].engineering_value);
    EXPECT_EQ(1, measurements[0].sequence);
    EXPECT_EQ(4, measurements[3].sequence);
    EXPECT_EQ(0x2400, i2c.last_sht_command);
    EXPECT_EQ(0, spi.config & 0x80u);
    EXPECT_EQ(SYS_OK, acquisition_subsystem_process(
        &context.acquisition, 1235u, measurements,
        ACQUISITION_MEASUREMENT_COUNT, &count));
    EXPECT_EQ(0, count);
    EXPECT_EQ(SYS_OK, acquisition_subsystem_suspend(&context.acquisition));
    EXPECT_EQ(ERR_DEVICE_NOT_READY, acquisition_subsystem_process(
        &context.acquisition, 2000u, measurements,
        ACQUISITION_MEASUREMENT_COUNT, &count));
    EXPECT_EQ(SYS_OK, acquisition_subsystem_resume(&context.acquisition));
}

static void test_device_failure_is_isolated(void)
{
    fake_i2c_t i2c = { 0u, 16000, 0x6666u, 0x8000u, 0u, 0u, 0u, 0u };
    fake_spi_t spi = { 0u, 0u, 8192u, 0u, 0u };
    app_acquisition_config_t config = make_config(&i2c, &spi);
    app_context_t context;
    gateway_measurement_t measurements[ACQUISITION_MEASUREMENT_COUNT];
    acquisition_health_t health;
    size_t count = 0u;

    EXPECT_EQ(SYS_OK, app_context_init(&context));
    EXPECT_EQ(SYS_OK, app_context_configure_acquisition(&context, &config));
    i2c.fail_ads_conversion = 1u;
    EXPECT_EQ(ERR_TIMEOUT, acquisition_subsystem_process(
        &context.acquisition, 2000u, measurements,
        ACQUISITION_MEASUREMENT_COUNT, &count));
    EXPECT_EQ(4, count);
    EXPECT_EQ(GATEWAY_QUALITY_COMM_ERROR, measurements[0].quality);
    EXPECT_EQ(ERR_TIMEOUT, measurements[0].error);
    EXPECT_EQ(GATEWAY_QUALITY_GOOD, measurements[1].quality);
    EXPECT_EQ(GATEWAY_QUALITY_GOOD, measurements[2].quality);
    EXPECT_EQ(GATEWAY_QUALITY_GOOD, measurements[3].quality);
    EXPECT_EQ(SYS_OK, acquisition_subsystem_get_health(
        &context.acquisition, &health));
    EXPECT_EQ(1, health.degraded_cycles);
    EXPECT_EQ(ERR_TIMEOUT, health.last_cycle_error);
    EXPECT_EQ(1, health.ads1115.consecutive_errors);
    EXPECT_EQ(0, health.max31865.consecutive_errors);
}

static void test_startup_failure_recovers(void)
{
    fake_i2c_t i2c = { 0u, 16000, 0x6666u, 0x8000u, 0u, 0u, 0u, 1u };
    fake_spi_t spi = { 0u, 0u, 8192u, 0u, 0u };
    app_acquisition_config_t config = make_config(&i2c, &spi);
    app_context_t context;
    gateway_measurement_t measurements[ACQUISITION_MEASUREMENT_COUNT];
    size_t count = 0u;

    EXPECT_EQ(SYS_OK, app_context_init(&context));
    EXPECT_EQ(ERR_IO, app_context_configure_acquisition(&context, &config));
    EXPECT_EQ(APP_INITIALIZED_ACQUISITION,
              context.initialization_mask & APP_INITIALIZED_ACQUISITION);
    EXPECT_EQ(0, context.ads1115.health.initialized);

    i2c.fail_ads_self_test = 0u;
    EXPECT_EQ(SYS_OK, acquisition_subsystem_process(
        &context.acquisition, 0u, measurements,
        ACQUISITION_MEASUREMENT_COUNT, &count));
    EXPECT_EQ(4, count);
    EXPECT_EQ(1, context.ads1115.health.initialized);
    EXPECT_EQ(GATEWAY_QUALITY_GOOD, measurements[0].quality);
}

static void test_periodic_timing_monitor(void)
{
    periodic_timing_monitor_t monitor;
    periodic_timing_stats_t stats;

    EXPECT_EQ(ERR_INVALID_ARG, periodic_timing_monitor_construct(
        &monitor, 0u, 2u));
    EXPECT_EQ(SYS_OK, periodic_timing_monitor_construct(
        &monitor, 100u, 2u));
    EXPECT_EQ(SYS_OK, periodic_timing_monitor_note(&monitor, 1000u, 1000u));
    EXPECT_EQ(SYS_OK, periodic_timing_monitor_note(&monitor, 1101u, 1100u));
    EXPECT_EQ(SYS_OK, periodic_timing_monitor_note(&monitor, 1199u, 1200u));
    EXPECT_EQ(SYS_OK, periodic_timing_monitor_note(&monitor, 1305u, 1300u));
    EXPECT_EQ(SYS_OK, periodic_timing_monitor_get(&monitor, &stats));
    EXPECT_EQ(100, stats.expected_period_ms);
    EXPECT_EQ(4, stats.releases);
    EXPECT_EQ(3, stats.intervals);
    EXPECT_EQ(106, stats.last_interval_ms);
    EXPECT_EQ(6, stats.last_jitter_ms);
    EXPECT_EQ(5, stats.last_release_lateness_ms);
    EXPECT_EQ(98, stats.min_interval_ms);
    EXPECT_EQ(106, stats.max_interval_ms);
    EXPECT_EQ(2, stats.max_early_ms);
    EXPECT_EQ(6, stats.max_late_ms);
    EXPECT_EQ(1, stats.deadline_misses);

    EXPECT_EQ(SYS_OK, periodic_timing_monitor_construct(
        &monitor, 100u, 2u));
    EXPECT_EQ(SYS_OK, periodic_timing_monitor_note(
        &monitor, 0xfffffff0u, 0xfffffff0u));
    EXPECT_EQ(SYS_OK, periodic_timing_monitor_note(
        &monitor, 0x00000054u, 0x00000054u));
    EXPECT_EQ(SYS_OK, periodic_timing_monitor_get(&monitor, &stats));
    EXPECT_EQ(100, stats.last_interval_ms);
    EXPECT_EQ(0, stats.last_jitter_ms);
}

int main(void)
{
    test_conversion_helpers();
    test_complete_acquisition_cycle();
    test_device_failure_is_isolated();
    test_startup_failure_recovers();
    test_periodic_timing_monitor();

    if (failures != 0) {
        printf("%d sensor acquisition test(s) failed\n", failures);
        return 1;
    }
    puts("sensor acquisition tests passed");
    return 0;
}
