/** @file test_device_contracts.c
 * @author 兆鸣嵌入式
 * Fault injection for device boundaries and recoverable power handshakes.
 */
#include "deep_power_controller.h"
#include "dma_rx_stream.h"
#include "mqtt_codec.h"
#include "network_transport.h"
#include "relay.h"
#include "storage_media.h"

#include <assert.h>
#include <stdint.h>

typedef struct {
    status_t write_status;
    status_t close_status;
    size_t received_length;
    uint32_t writes;
    uint32_t closes;
    uint32_t suspends;
} fake_device_t;

static status_t fake_init(void *context)
{
    (void)context;
    return SYS_OK;
}

static status_t fake_relay_init(void *context, int level)
{
    (void)level;
    return fake_init(context);
}

static status_t fake_write(void *context, int level)
{
    fake_device_t *fake = context;
    (void)level;
    fake->writes++;
    return fake->write_status;
}

static status_t fake_connect(void *context, const char *host, uint16_t port)
{
    (void)context;
    (void)host;
    (void)port;
    return SYS_OK;
}

static status_t fake_send(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    (void)data;
    (void)length;
    return SYS_OK;
}

static status_t fake_receive(void *context,
                             uint8_t *data,
                             size_t capacity,
                             size_t *length,
                             uint32_t timeout_ms)
{
    const fake_device_t *fake = context;
    (void)data;
    (void)capacity;
    (void)timeout_ms;
    *length = fake->received_length;
    return SYS_OK;
}

static status_t fake_close(void *context)
{
    fake_device_t *fake = context;
    fake->closes++;
    return fake->close_status;
}

static status_t fake_suspend(void *context)
{
    fake_device_t *fake = context;
    fake->suspends++;
    return SYS_OK;
}

static const relay_ops_t relay_ops = {
    fake_relay_init, fake_write, 0, fake_suspend, fake_init};
static const network_transport_ops_t network_ops = {fake_init,
                                                    fake_connect,
                                                    fake_send,
                                                    fake_receive,
                                                    fake_close,
                                                    fake_suspend,
                                                    fake_init};

static void test_relay_failed_resume(void)
{
    fake_device_t fake = {0};
    relay_t relay;
    relay_state_t state;
    relay_health_t health;
    relay_config_t config = {1u, RELAY_DEENERGIZED};
    uint32_t writes;

    assert(relay_construct(&relay, &relay_ops, &fake, &config) == SYS_OK);
    assert(relay_init(&relay) == SYS_OK);
    assert(relay_set(&relay, RELAY_ENERGIZED) == SYS_OK);
    assert(relay_init(&relay) == SYS_OK);
    assert(relay_get_state(&relay, &state) == SYS_OK);
    assert(state == RELAY_DEENERGIZED);
    assert(relay_suspend(&relay) == SYS_OK);
    assert(relay_configure_safe_state(&relay, RELAY_ENERGIZED) == SYS_OK);
    fake.write_status = ERR_IO;
    assert(relay_resume(&relay) == ERR_IO);
    assert(relay_get_state(&relay, &state) == SYS_OK);
    assert(state == RELAY_DEENERGIZED);
    writes = fake.writes;
    assert(relay_set(&relay, RELAY_ENERGIZED) == ERR_DEVICE_NOT_READY);
    assert(fake.writes == writes);
    fake.write_status = SYS_OK;
    assert(relay_resume(&relay) == SYS_OK);
    assert(fake.writes == writes + 1u);
    assert(relay_get_state(&relay, &state) == SYS_OK);
    assert(state == RELAY_ENERGIZED);
    assert(relay_get_health(&relay, &health) == SYS_OK);
    assert(health.write_errors == 1u);
    assert(relay_set(&relay, (relay_state_t)-1) == ERR_INVALID_ARG);
}

static void test_transport_faults(void)
{
    fake_device_t fake = {0};
    network_transport_t transport;
    network_transport_t empty = {0};
    network_connect_step_ops_t invalid_steps = {0};
    uint8_t data[4];
    size_t length = 99u;

    assert(network_transport_init(&empty) == ERR_INVALID_ARG);
    assert(network_transport_construct(&transport, &network_ops, &fake) ==
           SYS_OK);
    assert(network_transport_set_connect_steps(&transport, &invalid_steps) ==
           ERR_INVALID_ARG);
    assert(network_transport_init(&transport) == SYS_OK);
    assert(network_transport_connect_step(&transport, 0, 1u, 0u) ==
           ERR_INVALID_ARG);
    assert(network_transport_connect(&transport, "host", 80u) == SYS_OK);
    fake.close_status = ERR_IO;
    assert(network_transport_suspend(&transport) == ERR_IO);
    assert(network_transport_init(&transport) == ERR_DEVICE_NOT_READY);
    assert(network_transport_resume(&transport) == ERR_DEVICE_NOT_READY);
    assert(network_transport_connect(&transport, "other", 80u) ==
           ERR_DEVICE_NOT_READY);
    assert(fake.suspends == 0u);
    assert(network_transport_send(&transport, data, sizeof(data)) == SYS_OK);
    fake.received_length = sizeof(data) + 1u;
    assert(network_transport_receive(
               &transport, data, sizeof(data), &length, 0u) == ERR_PROTOCOL);
    assert(length == 0u);
    fake.close_status = SYS_OK;
    assert(network_transport_suspend(&transport) == SYS_OK);
    assert(fake.closes == 2u && fake.suspends == 1u);
    assert(network_transport_send(&transport, data, sizeof(data)) ==
           ERR_DEVICE_NOT_READY);
}

static void test_invalid_storage(void)
{
    storage_media_t media = {0};
    storage_media_ops_t ops = {0};
    uint8_t data = 0u;

    assert(storage_media_read(&media, 0u, &data, 1u) == ERR_INVALID_ARG);
    assert(storage_media_program(&media, 0u, &data, 1u) == ERR_INVALID_ARG);
    assert(storage_media_erase_sector(&media, 0u) == ERR_INVALID_ARG);
    media.ops = &ops;
    media.total_size = 4096u;
    assert(storage_media_erase_sector(&media, 0u) == ERR_INVALID_ARG);
    assert(storage_media_wake(&media) == ERR_INVALID_ARG);
    assert(storage_media_power_down(&media) == ERR_INVALID_ARG);
}

static void test_invalid_dma(void)
{
    dma_rx_stream_t stream = {0};
    uint8_t data[2] = {0};

    dma_rx_stream_reset(0);
    dma_rx_stream_publish(0, 1u);
    dma_rx_stream_publish(&stream, 1u);
    assert(dma_rx_stream_read(&stream, data, sizeof(data)) == 0u);
    stream.dma = data;
    stream.dma_capacity = sizeof(data);
    stream.ring = data;
    stream.ring_capacity = 1u;
    dma_rx_stream_publish(&stream, 1u);
    assert(dma_rx_stream_read(&stream, data, sizeof(data)) == 0u);
    stream.ring_capacity = sizeof(data);
    stream.write_position = sizeof(data);
    dma_rx_stream_publish(&stream, 1u);
    assert(dma_rx_stream_read(&stream, data, sizeof(data)) == 0u);
    dma_rx_stream_reset(&stream);
    assert(dma_rx_stream_read(&stream, 0, sizeof(data)) == 0u);
}

static void test_mqtt_length_bounds(void)
{
    uint8_t packet[8];
    const uint8_t payload = 0u;
    size_t length;
    mqtt_connect_options_t options = {"id", "", "secret", 30u, 1u};

    assert(
        mqtt_encode_publish_qos1(
            "a",
            &(const mqtt_publish_request_t){
                &payload, SIZE_MAX, 1u, 0u, packet, sizeof(packet), &length}) ==
        ERR_INVALID_ARG);
    assert(mqtt_encode_publish_qos1(
               "a",
               &(const mqtt_publish_request_t){&payload,
                                               UINT32_MAX - 4u,
                                               1u,
                                               0u,
                                               packet,
                                               sizeof(packet),
                                               &length}) == ERR_INVALID_ARG);
    assert(mqtt_encode_publish_qos1(
               "a",
               &(const mqtt_publish_request_t){&payload,
                                               MQTT_MAX_REMAINING_LENGTH - 5u,
                                               1u,
                                               0u,
                                               packet,
                                               sizeof(packet),
                                               &length}) == ERR_NO_MEMORY);
    assert(mqtt_encode_connect(&options, packet, sizeof(packet), &length) ==
           ERR_INVALID_ARG);
}

static status_t fake_stop(void *context,
                          uint32_t requested_ms,
                          uint32_t *elapsed_ms,
                          power_wake_reason_t *reason)
{
    (void)context;
    *elapsed_ms = requested_ms;
    *reason = POWER_WAKE_INTERRUPT;
    return SYS_OK;
}

static void test_quiesce_deadline(void)
{
    const deep_power_platform_ops_t ops = {fake_stop, fake_init};
    deep_power_controller_config_t config = {
        1u, 1u, 1000u, 8000u, 500u, DEEP_POWER_PARTICIPANT_STORAGE, 5000u};
    deep_power_controller_t controller;
    deep_power_health_t health;
    const uint32_t start = UINT32_MAX - 1000u;

    assert(deep_power_controller_construct(&controller, &ops, 0, &config) ==
           SYS_OK);
    assert(deep_power_controller_request(&controller,
                                         POWER_STOP_PERIODIC,
                                         2000u,
                                         DEEP_POWER_CONFIRMATION,
                                         start) == SYS_OK);
    assert(deep_power_controller_process(
               &controller, 0u, POWER_STANDBY_SHIPPING, 8000u, start + 4999u) ==
           ERR_DEVICE_NOT_READY);
    assert(deep_power_controller_process(
               &controller, 0u, POWER_STANDBY_SHIPPING, 8000u, start + 5000u) ==
           ERR_TIMEOUT);
    assert(deep_power_controller_get_health(&controller, &health) == SYS_OK);
    assert(health.request_pending == 0u && health.failures == 1u);
    assert(deep_power_controller_request(&controller,
                                         POWER_STOP_PERIODIC,
                                         2000u,
                                         DEEP_POWER_CONFIRMATION,
                                         10000u) == SYS_OK);
    assert(deep_power_controller_process(&controller,
                                         DEEP_POWER_PARTICIPANT_STORAGE,
                                         POWER_STANDBY_SHIPPING,
                                         8000u,
                                         10001u) == SYS_OK);
    config.watchdog_margin_ms = UINT32_MAX;
    assert(deep_power_controller_construct(&controller, &ops, 0, &config) ==
           ERR_INVALID_ARG);
    config.watchdog_margin_ms = 500u;
    config.required_quiesce_mask = 1UL << 31;
    assert(deep_power_controller_construct(&controller, &ops, 0, &config) ==
           ERR_INVALID_ARG);
}

int main(void)
{
    test_relay_failed_resume();
    test_transport_faults();
    test_invalid_storage();
    test_invalid_dma();
    test_mqtt_length_bounds();
    test_quiesce_deadline();
    return 0;
}
