#include "can_protocol.h"
#include "fieldbus_subsystem.h"
#include "modbus_rtu_master.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define EXPECT_TRUE(value) do { \
    if (!(value)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); \
        failures++; \
    } \
} while (0)

#define EXPECT_EQ(expected, actual) do { \
    long long expected_value = (long long)(expected); \
    long long actual_value = (long long)(actual); \
    if (expected_value != actual_value) { \
        printf("FAIL %s:%d: expected %lld got %lld\n", \
               __FILE__, __LINE__, expected_value, actual_value); \
        failures++; \
    } \
} while (0)

typedef struct {
    uint8_t response[MODBUS_RTU_MAX_ADU_SIZE];
    size_t response_length;
    status_t scripted_status[4];
    size_t scripted_count;
    size_t calls;
    uint8_t last_request[8];
} fake_rs485_t;

static status_t fake_rs485_exchange(void *context,
                                    const uint8_t *request,
                                    size_t request_length,
                                    uint8_t *response,
                                    size_t response_capacity,
                                    size_t *response_length,
                                    uint32_t timeout_ms)
{
    fake_rs485_t *fake = context;
    status_t status = SYS_OK;

    (void)timeout_ms;
    if (fake == 0 || request_length != sizeof(fake->last_request) ||
        response_capacity < fake->response_length) {
        return ERR_INVALID_ARG;
    }
    memcpy(fake->last_request, request, request_length);
    if (fake->calls < fake->scripted_count) {
        status = fake->scripted_status[fake->calls];
    }
    fake->calls++;
    if (status != SYS_OK) {
        return status;
    }
    memcpy(response, fake->response, fake->response_length);
    *response_length = fake->response_length;
    return SYS_OK;
}

static const rs485_bus_ops_t fake_rs485_ops = {
    fake_rs485_exchange, 0, 0
};

static void append_crc(uint8_t *frame, size_t payload_length)
{
    uint16_t crc = modbus_rtu_crc16(frame, payload_length);

    frame[payload_length] = (uint8_t)crc;
    frame[payload_length + 1u] = (uint8_t)(crc >> 8u);
}

static const modbus_poll_entry_t poll_table[] = {
    {
        1u, MODBUS_FUNCTION_READ_HOLDING, 0x0010u, 1u, 0u,
        MODBUS_VALUE_S16, MODBUS_WORD_HIGH_FIRST,
        10, 1, 0, GATEWAY_POINT_MODBUS_PROCESS_VALUE,
        GATEWAY_UNIT_RAW
    },
    {
        2u, MODBUS_FUNCTION_READ_INPUT, 0x0020u, 2u, 0u,
        MODBUS_VALUE_S32, MODBUS_WORD_LOW_FIRST,
        1, 100, -50, (uint16_t)(GATEWAY_POINT_MODBUS_PROCESS_VALUE + 1u),
        GATEWAY_UNIT_MILLICELSIUS
    }
};

static void configure_modbus(fake_rs485_t *fake, rs485_bus_t *bus,
                             modbus_master_t *master, uint8_t retries)
{
    EXPECT_EQ(SYS_OK, rs485_bus_construct(bus, &fake_rs485_ops, fake, 100u));
    EXPECT_EQ(SYS_OK, modbus_master_construct(
        master, bus, poll_table, sizeof(poll_table) / sizeof(poll_table[0]),
        retries));
}

static void test_modbus_crc_and_scaling(void)
{
    static const uint8_t known_request[] = {
        0x01u, 0x03u, 0x00u, 0x00u, 0x00u, 0x0au
    };
    fake_rs485_t fake = { 0 };
    rs485_bus_t bus;
    modbus_master_t master;
    gateway_measurement_t measurement;

    EXPECT_EQ(0xcdc5u, modbus_rtu_crc16(known_request,
                                        sizeof(known_request)));
    fake.response[0] = 1u;
    fake.response[1] = 3u;
    fake.response[2] = 2u;
    fake.response[3] = 0u;
    fake.response[4] = 123u;
    append_crc(fake.response, 5u);
    fake.response_length = 7u;
    configure_modbus(&fake, &bus, &master, 1u);

    EXPECT_EQ(SYS_OK, modbus_master_poll_next(&master, 1000u,
                                              &measurement));
    EXPECT_EQ(1u, fake.last_request[0]);
    EXPECT_EQ(3u, fake.last_request[1]);
    EXPECT_EQ(0x10u, fake.last_request[3]);
    EXPECT_EQ(123, measurement.raw_value);
    EXPECT_EQ(1230, measurement.engineering_value);
    EXPECT_EQ(GATEWAY_QUALITY_GOOD, measurement.quality);
    EXPECT_EQ(GATEWAY_SOURCE_MODBUS, measurement.source);
}

static void test_modbus_retry_and_failure_quality(void)
{
    fake_rs485_t fake = { 0 };
    rs485_bus_t bus;
    modbus_master_t master;
    gateway_measurement_t measurement;

    fake.response[0] = 1u;
    fake.response[1] = 3u;
    fake.response[2] = 2u;
    fake.response[3] = 0u;
    fake.response[4] = 42u;
    append_crc(fake.response, 5u);
    fake.response_length = 7u;
    fake.scripted_status[0] = ERR_TIMEOUT;
    fake.scripted_status[1] = SYS_OK;
    fake.scripted_count = 2u;
    configure_modbus(&fake, &bus, &master, 1u);

    EXPECT_EQ(SYS_OK, modbus_master_poll_next(&master, 2000u,
                                              &measurement));
    EXPECT_EQ(2u, fake.calls);
    EXPECT_EQ(1u, master.health.retries);
    EXPECT_EQ(1u, master.health.timeouts);

    memset(&fake, 0, sizeof(fake));
    fake.scripted_status[0] = ERR_TIMEOUT;
    fake.scripted_status[1] = ERR_TIMEOUT;
    fake.scripted_count = 2u;
    configure_modbus(&fake, &bus, &master, 1u);
    EXPECT_EQ(ERR_TIMEOUT, modbus_master_poll_next(&master, 3000u,
                                                   &measurement));
    EXPECT_EQ(GATEWAY_QUALITY_COMM_ERROR, measurement.quality);
    EXPECT_EQ(ERR_TIMEOUT, measurement.error);
    EXPECT_EQ(1u, master.next_poll);
}

static void test_modbus_input_register_and_exception(void)
{
    fake_rs485_t fake = { 0 };
    rs485_bus_t bus;
    modbus_master_t master;
    gateway_measurement_t measurement;

    configure_modbus(&fake, &bus, &master, 0u);
    master.next_poll = 1u;
    fake.response[0] = 2u;
    fake.response[1] = 4u;
    fake.response[2] = 4u;
    fake.response[3] = 0x56u;
    fake.response[4] = 0x78u;
    fake.response[5] = 0x12u;
    fake.response[6] = 0x34u;
    append_crc(fake.response, 7u);
    fake.response_length = 9u;
    EXPECT_EQ(SYS_OK, modbus_master_poll_next(&master, 4000u,
                                              &measurement));
    EXPECT_EQ(4u, fake.last_request[1]);
    EXPECT_EQ((int32_t)0x12345678, measurement.raw_value);
    EXPECT_EQ(((int32_t)0x12345678 / 100) - 50,
              measurement.engineering_value);

    memset(&fake, 0, sizeof(fake));
    configure_modbus(&fake, &bus, &master, 0u);
    fake.response[0] = 1u;
    fake.response[1] = 0x83u;
    fake.response[2] = 2u;
    append_crc(fake.response, 3u);
    fake.response_length = 5u;
    EXPECT_EQ(ERR_PROTOCOL, modbus_master_poll_next(&master, 5000u,
                                                    &measurement));
    EXPECT_EQ(1u, master.health.exceptions);
}

static void test_modbus_crc_failure(void)
{
    fake_rs485_t fake = { 0 };
    rs485_bus_t bus;
    modbus_master_t master;
    gateway_measurement_t measurement;

    fake.response[0] = 1u;
    fake.response[1] = 3u;
    fake.response[2] = 2u;
    fake.response[3] = 0u;
    fake.response[4] = 1u;
    fake.response[5] = 0u;
    fake.response[6] = 0u;
    fake.response_length = 7u;
    configure_modbus(&fake, &bus, &master, 0u);
    EXPECT_EQ(ERR_CRC, modbus_master_poll_next(&master, 5500u,
                                               &measurement));
    EXPECT_EQ(1u, master.health.crc_errors);
    EXPECT_EQ(GATEWAY_QUALITY_COMM_ERROR, measurement.quality);
}

typedef struct {
    can_frame_t rx_frames[4];
    size_t rx_count;
    size_t rx_index;
    can_frame_t last_tx;
    can_bus_state_t state;
    uint32_t starts;
    uint32_t recoveries;
} fake_can_t;

static status_t fake_can_start(void *context)
{
    fake_can_t *fake = context;
    fake->starts++;
    fake->state = CAN_BUS_ACTIVE;
    return SYS_OK;
}

static status_t fake_can_send(void *context, const can_frame_t *frame)
{
    fake_can_t *fake = context;
    fake->last_tx = *frame;
    return SYS_OK;
}

static status_t fake_can_receive(void *context, can_frame_t *frame)
{
    fake_can_t *fake = context;

    if (fake->rx_index >= fake->rx_count) {
        return ERR_DEVICE_NOT_READY;
    }
    *frame = fake->rx_frames[fake->rx_index++];
    return SYS_OK;
}

static status_t fake_can_wait(void *context, uint32_t timeout_ms,
                              uint32_t *event_bits)
{
    fake_can_t *fake = context;
    (void)timeout_ms;
    *event_bits = fake->rx_index < fake->rx_count
        ? CAN_BUS_EVENT_RX : CAN_BUS_EVENT_NONE;
    return SYS_OK;
}

static status_t fake_can_state(void *context, can_bus_state_t *state)
{
    *state = ((fake_can_t *)context)->state;
    return SYS_OK;
}

static status_t fake_can_recover(void *context)
{
    fake_can_t *fake = context;
    fake->recoveries++;
    fake->state = CAN_BUS_ACTIVE;
    return SYS_OK;
}

static const can_bus_ops_t fake_can_ops = {
    fake_can_start, fake_can_send, fake_can_receive, fake_can_wait,
    fake_can_state, fake_can_recover, 0, 0
};

static void test_can_protocol_and_fieldbus_recovery(void)
{
    fake_rs485_t fake_rs485 = { 0 };
    fake_can_t fake_can = { 0 };
    rs485_bus_t rs485;
    modbus_master_t modbus;
    can_bus_t can_bus;
    fieldbus_subsystem_t fieldbus;
    gateway_measurement_t source = { 0 };
    gateway_measurement_t received[2];
    can_frame_t frame;
    uint8_t node_id = 0u;
    size_t count = 0u;

    source.point_id = GATEWAY_POINT_CAN_REMOTE_VALUE;
    source.sequence = 0x34u;
    source.engineering_value = -123456;
    source.quality = GATEWAY_QUALITY_GOOD;
    EXPECT_EQ(SYS_OK, can_protocol_encode_measurement(7u, &source, &frame));
    EXPECT_EQ(0x107u, frame.id);
    EXPECT_EQ(SYS_OK, can_protocol_decode_measurement(
        &frame, 6000u, &node_id, &received[0]));
    EXPECT_EQ(7u, node_id);
    EXPECT_EQ(-123456, received[0].engineering_value);

    configure_modbus(&fake_rs485, &rs485, &modbus, 0u);
    EXPECT_EQ(SYS_OK, can_bus_construct(&can_bus, &fake_can_ops, &fake_can));
    EXPECT_EQ(SYS_OK, fieldbus_subsystem_construct(
        &fieldbus, &modbus, &can_bus, 1u, 1000u));
    EXPECT_EQ(SYS_OK, fieldbus_subsystem_start(&fieldbus));
    fake_can.rx_frames[0] = frame;
    fake_can.rx_count = 1u;
    EXPECT_EQ(SYS_OK, fieldbus_subsystem_process_can(
        &fieldbus, CAN_BUS_EVENT_RX, 6000u, received, 2u, &count));
    EXPECT_EQ(1u, count);
    EXPECT_EQ(GATEWAY_SOURCE_CAN, received[0].source);

    EXPECT_EQ(SYS_OK, fieldbus_subsystem_send_can(&fieldbus, &source));
    EXPECT_EQ(1u, fieldbus.health.can_tx_frames);
    EXPECT_EQ(0x101u, fake_can.last_tx.id);

    fake_can.state = CAN_BUS_OFF;
    EXPECT_EQ(ERR_BUS_OFF, fieldbus_subsystem_process_can(
        &fieldbus, CAN_BUS_EVENT_ERROR, 7000u, received, 2u, &count));
    EXPECT_EQ(1u, fieldbus.health.can_bus_off_events);
    EXPECT_EQ(ERR_BUS_OFF, fieldbus_subsystem_process_can(
        &fieldbus, CAN_BUS_EVENT_ERROR, 7999u, received, 2u, &count));
    EXPECT_EQ(SYS_OK, fieldbus_subsystem_process_can(
        &fieldbus, CAN_BUS_EVENT_ERROR, 8000u, received, 2u, &count));
    EXPECT_EQ(1u, fake_can.recoveries);
    EXPECT_EQ(1u, fieldbus.health.can_recoveries);
}

int main(void)
{
    test_modbus_crc_and_scaling();
    test_modbus_retry_and_failure_quality();
    test_modbus_input_register_and_exception();
    test_modbus_crc_failure();
    test_can_protocol_and_fieldbus_recovery();

    if (failures != 0) {
        printf("fieldbus protocol tests: %d failure(s)\n", failures);
        return 1;
    }
    printf("fieldbus protocol tests: PASS\n");
    return 0;
}
