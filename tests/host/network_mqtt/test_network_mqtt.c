#include "esp8266.h"
#include "mqtt_codec.h"
#include "network_subsystem.h"
#include "network_transport.h"

#include <stdio.h>
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

#define FAKE_PACKET_COUNT 24u

typedef struct {
    uint8_t sent[FAKE_PACKET_COUNT][NETWORK_MQTT_PACKET_MAX];
    size_t sent_length[FAKE_PACKET_COUNT];
    size_t sent_count;
    uint8_t rx[256];
    size_t rx_length;
    unsigned int init_calls;
    unsigned int connect_calls;
    unsigned int close_calls;
    unsigned int suspend_calls;
    uint8_t auto_puback;
    uint8_t fail_receive_once;
} fake_transport_t;

static size_t mqtt_body_offset(const uint8_t *packet, size_t length)
{
    size_t index = 1u;

    while (index < length && (packet[index++] & 0x80u) != 0u) {
    }
    return index;
}

static uint16_t publish_packet_id(const uint8_t *packet, size_t length)
{
    size_t offset = mqtt_body_offset(packet, length);
    size_t topic_length;

    if (offset + 2u > length) {
        return 0u;
    }
    topic_length = ((size_t)packet[offset] << 8u) | packet[offset + 1u];
    offset += 2u + topic_length;
    if (offset + 2u > length) {
        return 0u;
    }
    return (uint16_t)((uint16_t)packet[offset] << 8u) | packet[offset + 1u];
}

static int contains_bytes(const uint8_t *buffer, size_t length,
                          const char *text)
{
    size_t text_length = strlen(text);
    size_t index;

    if (text_length > length) {
        return 0;
    }
    for (index = 0u; index <= length - text_length; ++index) {
        if (memcmp(&buffer[index], text, text_length) == 0) {
            return 1;
        }
    }
    return 0;
}

static void queue_rx(fake_transport_t *port, const uint8_t *data,
                     size_t length)
{
    if (length <= sizeof(port->rx) - port->rx_length) {
        memcpy(&port->rx[port->rx_length], data, length);
        port->rx_length += length;
    }
}

static status_t fake_transport_init(void *context)
{
    fake_transport_t *port = context;

    port->init_calls++;
    return SYS_OK;
}

static status_t fake_transport_connect(void *context, const char *host,
                                       uint16_t port_number)
{
    static const uint8_t connack[] = { 0x20u, 0x02u, 0x00u, 0x00u };
    fake_transport_t *port = context;

    if (host == 0 || port_number == 0u) {
        return ERR_INVALID_ARG;
    }
    port->connect_calls++;
    queue_rx(port, connack, sizeof(connack));
    return SYS_OK;
}

static status_t fake_transport_send(void *context, const uint8_t *data,
                                    size_t length)
{
    fake_transport_t *port = context;

    if (port->sent_count >= FAKE_PACKET_COUNT ||
        length > sizeof(port->sent[0])) {
        return ERR_NO_MEMORY;
    }
    memcpy(port->sent[port->sent_count], data, length);
    port->sent_length[port->sent_count] = length;
    port->sent_count++;
    if ((data[0] >> 4u) == MQTT_PACKET_PUBLISH && port->auto_puback != 0u) {
        uint16_t packet_id = publish_packet_id(data, length);
        uint8_t puback[] = {
            0x40u, 0x02u, (uint8_t)(packet_id >> 8u), (uint8_t)packet_id
        };

        queue_rx(port, puback, sizeof(puback));
    }
    return SYS_OK;
}

static status_t fake_transport_receive(void *context, uint8_t *data,
                                       size_t capacity, size_t *length,
                                       uint32_t timeout_ms)
{
    fake_transport_t *port = context;

    (void)timeout_ms;
    if (port->fail_receive_once != 0u) {
        port->fail_receive_once = 0u;
        *length = 0u;
        return ERR_IO;
    }
    if (port->rx_length == 0u) {
        *length = 0u;
        return ERR_TIMEOUT;
    }
    *length = port->rx_length < capacity ? port->rx_length : capacity;
    memcpy(data, port->rx, *length);
    memmove(port->rx, &port->rx[*length], port->rx_length - *length);
    port->rx_length -= *length;
    return SYS_OK;
}

static status_t fake_transport_close(void *context)
{
    fake_transport_t *port = context;

    port->close_calls++;
    return SYS_OK;
}

static status_t fake_transport_suspend(void *context)
{
    fake_transport_t *port = context;

    port->suspend_calls++;
    return SYS_OK;
}

static const network_transport_ops_t fake_transport_ops = {
    fake_transport_init,
    fake_transport_connect,
    fake_transport_send,
    fake_transport_receive,
    fake_transport_close,
    fake_transport_suspend,
    0
};

static network_subsystem_config_t network_config(void)
{
    network_subsystem_config_t config;

    memset(&config, 0, sizeof(config));
    config.device_id = "line_01";
    config.client_id = "gateway_line_01";
    config.username = "";
    config.password = "";
    config.broker_host = "192.168.1.100";
    config.broker_port = 1883u;
    config.keep_alive_seconds = 30u;
    config.boot_id = 7u;
    config.connect_timeout_ms = 1000u;
    config.puback_timeout_ms = 100u;
    config.reconnect_initial_ms = 50u;
    config.reconnect_max_ms = 1000u;
    config.publish_retry_limit = 2u;
    return config;
}

static gateway_network_event_t telemetry_event(uint32_t sequence)
{
    gateway_network_event_t event;

    memset(&event, 0, sizeof(event));
    event.type = GATEWAY_NETWORK_TELEMETRY;
    event.sequence = sequence;
    event.qos = 1u;
    event.payload.measurement.point_id = GATEWAY_POINT_LOOP_CURRENT;
    event.payload.measurement.engineering_value = 12345;
    event.payload.measurement.unit = GATEWAY_UNIT_MICROAMP;
    event.payload.measurement.quality = GATEWAY_QUALITY_GOOD;
    return event;
}

static gateway_network_event_t alarm_event(uint32_t event_id)
{
    gateway_network_event_t event;

    memset(&event, 0, sizeof(event));
    event.type = GATEWAY_NETWORK_ALARM;
    event.sequence = event_id;
    event.qos = 1u;
    event.payload.alarm.event_id = event_id;
    event.payload.alarm.point_id = GATEWAY_POINT_LOOP_CURRENT;
    event.payload.alarm.type = GATEWAY_ALARM_HIGH;
    event.payload.alarm.transition = GATEWAY_ALARM_ENTERED;
    event.payload.alarm.quality = GATEWAY_QUALITY_GOOD;
    event.payload.alarm.value = 22000;
    event.payload.alarm.threshold = 20000;
    return event;
}

static int test_mqtt_codec(void)
{
    mqtt_connect_options_t options = {
        "gateway_01", "user", "pass", 30u, 1u
    };
    mqtt_packet_view_t view;
    uint8_t packet[128];
    size_t length = 0u;
    size_t consumed = 0u;
    static const uint8_t connack[] = { 0x20u, 0x02u, 0x00u, 0x00u };
    static const uint8_t puback[] = { 0x40u, 0x02u, 0x12u, 0x34u };

    EXPECT_STATUS(SYS_OK, mqtt_encode_connect(&options, packet,
                                               sizeof(packet), &length));
    EXPECT_TRUE(packet[0] == 0x10u);
    EXPECT_TRUE(length > 12u);
    EXPECT_STATUS(SYS_OK, mqtt_encode_publish_qos1(
        "factory/line/telemetry", (const uint8_t *)"{}", 2u,
        0x1234u, 0u, packet, sizeof(packet), &length));
    EXPECT_TRUE(packet[0] == 0x32u);
    EXPECT_TRUE(publish_packet_id(packet, length) == 0x1234u);
    EXPECT_STATUS(SYS_OK, mqtt_encode_publish_qos1(
        "factory/line/telemetry", (const uint8_t *)"{}", 2u,
        0x1234u, 1u, packet, sizeof(packet), &length));
    EXPECT_TRUE(packet[0] == 0x3au);
    EXPECT_STATUS(SYS_OK, mqtt_decode_packet(connack, sizeof(connack),
                                             &view, &consumed));
    EXPECT_TRUE(view.type == MQTT_PACKET_CONNACK && view.return_code == 0u);
    EXPECT_STATUS(SYS_OK, mqtt_decode_packet(puback, sizeof(puback),
                                             &view, &consumed));
    EXPECT_TRUE(view.packet_id == 0x1234u);
    EXPECT_STATUS(ERR_DEVICE_NOT_READY,
                  mqtt_decode_packet(puback, 1u, &view, &consumed));
    EXPECT_STATUS(ERR_INVALID_ARG,
                  mqtt_decode_packet(0, 0u, &view, &consumed));
    return 0;
}

static int test_alarm_priority_and_ack(void)
{
    fake_transport_t port = { 0 };
    network_transport_t transport;
    network_subsystem_t network;
    network_subsystem_config_t config = network_config();
    gateway_network_event_t telemetry = telemetry_event(1u);
    gateway_network_event_t alarm = alarm_event(2u);
    network_health_t health;

    port.auto_puback = 1u;
    EXPECT_STATUS(SYS_OK, network_transport_construct(
        &transport, &fake_transport_ops, &port));
    EXPECT_STATUS(SYS_OK, network_subsystem_construct(
        &network, &transport, &config));
    EXPECT_STATUS(SYS_OK, network_subsystem_start(&network, 0u));
    EXPECT_STATUS(SYS_OK, network_subsystem_submit(&network, &telemetry));
    EXPECT_STATUS(SYS_OK, network_subsystem_submit(&network, &alarm));
    EXPECT_STATUS(SYS_OK, network_subsystem_process(&network, 1u));
    EXPECT_TRUE(port.sent_count == 2u);
    EXPECT_TRUE(contains_bytes(port.sent[1], port.sent_length[1],
                               "/alarm"));
    EXPECT_STATUS(SYS_OK, network_subsystem_process(&network, 2u));
    EXPECT_TRUE(port.sent_count == 3u);
    EXPECT_TRUE(contains_bytes(port.sent[2], port.sent_length[2],
                               "/telemetry"));
    EXPECT_STATUS(SYS_OK, network_subsystem_process(&network, 3u));
    EXPECT_STATUS(SYS_OK, network_subsystem_get_health(&network, &health));
    EXPECT_TRUE(health.alarms_published == 1u);
    EXPECT_TRUE(health.telemetry_published == 1u);
    EXPECT_TRUE(health.pubacks == 2u && health.inflight_active == 0u);
    return 0;
}

static int test_retry_and_ota_lease(void)
{
    fake_transport_t port = { 0 };
    network_transport_t transport;
    network_subsystem_t network;
    network_subsystem_config_t config = network_config();
    gateway_network_event_t telemetry = telemetry_event(9u);
    network_health_t health;
    uint16_t packet_id;
    uint8_t puback[4];

    EXPECT_STATUS(SYS_OK, network_transport_construct(
        &transport, &fake_transport_ops, &port));
    EXPECT_STATUS(SYS_OK, network_subsystem_construct(
        &network, &transport, &config));
    EXPECT_STATUS(SYS_OK, network_subsystem_start(&network, 0u));
    EXPECT_STATUS(SYS_OK, network_subsystem_submit(&network, &telemetry));
    EXPECT_STATUS(SYS_OK, network_subsystem_process(&network, 1u));
    packet_id = publish_packet_id(port.sent[1], port.sent_length[1]);
    EXPECT_TRUE(packet_id != 0u && port.sent[1][0] == 0x32u);

    EXPECT_STATUS(SYS_OK,
                  network_subsystem_acquire_ota_lease(&network, 2u));
    EXPECT_STATUS(SYS_OK,
                  network_subsystem_release_ota_lease(&network, 3u));
    EXPECT_STATUS(SYS_OK, network_subsystem_process(&network, 3u));
    EXPECT_TRUE(port.sent_count == 5u);
    EXPECT_TRUE(port.sent[4][0] == 0x3au);
    EXPECT_TRUE(publish_packet_id(port.sent[4], port.sent_length[4]) ==
                packet_id);
    puback[0] = 0x40u;
    puback[1] = 0x02u;
    puback[2] = (uint8_t)(packet_id >> 8u);
    puback[3] = (uint8_t)packet_id;
    queue_rx(&port, puback, sizeof(puback));
    EXPECT_STATUS(SYS_OK, network_subsystem_process(&network, 4u));
    EXPECT_STATUS(SYS_OK, network_subsystem_get_health(&network, &health));
    EXPECT_TRUE(health.ota_leases == 1u && health.pubacks == 1u);
    EXPECT_TRUE(health.telemetry_published == 1u);

    EXPECT_STATUS(SYS_OK, network_subsystem_submit(&network, &telemetry));
    EXPECT_STATUS(SYS_OK, network_subsystem_process(&network, 5u));
    EXPECT_STATUS(SYS_OK, network_subsystem_process(&network, 105u));
    EXPECT_TRUE(port.sent[port.sent_count - 1u][0] == 0x3au);
    EXPECT_STATUS(SYS_OK, network_subsystem_get_health(&network, &health));
    EXPECT_TRUE(health.publish_retries == 1u);
    return 0;
}

static int test_disconnect_reinitializes_transport(void)
{
    fake_transport_t port = { 0 };
    network_transport_t transport;
    network_subsystem_t network;
    network_subsystem_config_t config = network_config();
    network_health_t health;

    EXPECT_STATUS(SYS_OK, network_transport_construct(
        &transport, &fake_transport_ops, &port));
    EXPECT_STATUS(SYS_OK, network_subsystem_construct(
        &network, &transport, &config));
    EXPECT_STATUS(SYS_OK, network_subsystem_start(&network, 0u));
    port.fail_receive_once = 1u;
    EXPECT_STATUS(ERR_IO, network_subsystem_process(&network, 1u));
    EXPECT_TRUE(port.suspend_calls == 1u && transport.initialized == 0u);
    EXPECT_STATUS(SYS_OK, network_subsystem_process(&network, 51u));
    EXPECT_STATUS(SYS_OK, network_subsystem_get_health(&network, &health));
    EXPECT_TRUE(port.init_calls == 2u && port.connect_calls == 2u);
    EXPECT_TRUE(health.mqtt_ready != 0u);
    return 0;
}

typedef struct {
    uint8_t rx[2048];
    size_t rx_length;
    size_t max_read;
    unsigned int init_calls;
    unsigned int suspend_calls;
    unsigned int resume_calls;
} fake_serial_t;

static void serial_queue(fake_serial_t *serial, const char *text)
{
    size_t length = strlen(text);

    if (length <= sizeof(serial->rx) - serial->rx_length) {
        memcpy(&serial->rx[serial->rx_length], text, length);
        serial->rx_length += length;
    }
}

static status_t fake_serial_init(void *context)
{
    fake_serial_t *serial = context;

    serial->init_calls++;
    return SYS_OK;
}

static status_t fake_serial_write(void *context, const uint8_t *data,
                                  size_t length, uint32_t timeout_ms)
{
    fake_serial_t *serial = context;

    (void)timeout_ms;
    if (length >= 2u && data[0] == 'A' && data[1] == 'T') {
        if (length >= 11u && memcmp(data, "AT+CIPSTART", 11u) == 0) {
            serial_queue(serial, "CONNECT\r\n");
        } else if (length >= 10u && memcmp(data, "AT+CIPSEND", 10u) == 0) {
            serial_queue(serial, ">\r\n");
        } else if (length >= 11u && memcmp(data, "AT+CIPCLOSE", 11u) == 0) {
            serial_queue(serial, "CLOSED\r\n");
        } else {
            serial_queue(serial, "OK\r\n");
        }
    } else {
        serial_queue(serial, "SEND OK\r\n");
    }
    return SYS_OK;
}

static status_t fake_serial_read(void *context, uint8_t *data,
                                 size_t capacity, size_t *length,
                                 uint32_t timeout_ms)
{
    fake_serial_t *serial = context;
    size_t available;

    (void)timeout_ms;
    if (serial->rx_length == 0u) {
        *length = 0u;
        return ERR_TIMEOUT;
    }
    available = serial->max_read != 0u && serial->max_read < capacity
        ? serial->max_read : capacity;
    *length = serial->rx_length < available ? serial->rx_length : available;
    memcpy(data, serial->rx, *length);
    memmove(serial->rx, &serial->rx[*length], serial->rx_length - *length);
    serial->rx_length -= *length;
    return SYS_OK;
}

static status_t fake_serial_flush(void *context)
{
    fake_serial_t *serial = context;

    serial->rx_length = 0u;
    return SYS_OK;
}

static status_t fake_serial_suspend(void *context)
{
    fake_serial_t *serial = context;

    serial->suspend_calls++;
    return SYS_OK;
}

static status_t fake_serial_resume(void *context)
{
    fake_serial_t *serial = context;

    serial->resume_calls++;
    return SYS_OK;
}

static const esp8266_serial_ops_t fake_serial_ops = {
    fake_serial_init,
    fake_serial_write,
    fake_serial_read,
    fake_serial_flush,
    fake_serial_suspend,
    fake_serial_resume
};

static int test_esp8266_raw_tcp(void)
{
    fake_serial_t serial = { 0 };
    esp8266_t esp;
    esp8266_config_t config = {
        "test_ssid", "test_password", 1000u, 5000u, 0u
    };
    uint8_t payload[4];
    size_t length = 0u;
    unsigned int attempt;

    EXPECT_STATUS(SYS_OK, esp8266_construct(
        &esp, &fake_serial_ops, &serial, &config));
    EXPECT_STATUS(SYS_OK, esp8266_init(&esp));
    EXPECT_STATUS(SYS_OK, esp8266_tcp_connect(&esp, "192.168.1.100",
                                              1883u));
    EXPECT_STATUS(SYS_OK, esp8266_tcp_send(
        &esp, (const uint8_t *)"MQTT", 4u));

    serial.max_read = 3u;
    serial_queue(&serial, "+IPD,4:ACK!");
    for (attempt = 0u; attempt < 8u; ++attempt) {
        status_t status = esp8266_tcp_receive(
            &esp, payload, sizeof(payload), &length, 10u);

        if (status == SYS_OK) {
            break;
        }
        EXPECT_TRUE(status == ERR_TIMEOUT);
    }
    EXPECT_TRUE(length == 4u && memcmp(payload, "ACK!", 4u) == 0);
    serial.max_read = 0u;
    serial_queue(&serial, "CLOSED\r\n");
    EXPECT_STATUS(ERR_IO, esp8266_tcp_receive(
        &esp, payload, sizeof(payload), &length, 10u));
    EXPECT_STATUS(SYS_OK, esp8266_suspend(&esp));
    EXPECT_STATUS(SYS_OK, esp8266_resume(&esp));
    EXPECT_TRUE(serial.init_calls == 1u && serial.suspend_calls == 1u &&
                serial.resume_calls == 1u);
    return 0;
}

static int test_network_suspend_resume(void)
{
    fake_transport_t port = { 0 };
    network_transport_t transport;
    network_subsystem_t network;
    network_subsystem_config_t config = network_config();
    network_health_t health;

    EXPECT_STATUS(SYS_OK, network_transport_construct(
        &transport, &fake_transport_ops, &port));
    EXPECT_STATUS(SYS_OK, network_subsystem_construct(
        &network, &transport, &config));
    EXPECT_STATUS(SYS_OK, network_subsystem_start(&network, 0u));
    EXPECT_STATUS(SYS_OK, network_subsystem_suspend(&network, 10u));
    EXPECT_TRUE(port.suspend_calls == 1u && transport.initialized == 0u);
    EXPECT_STATUS(ERR_DEVICE_NOT_READY,
                  network_subsystem_process(&network, 11u));
    EXPECT_STATUS(SYS_OK, network_subsystem_resume(&network, 20u));
    EXPECT_STATUS(SYS_OK, network_subsystem_process(&network, 20u));
    EXPECT_STATUS(SYS_OK, network_subsystem_get_health(&network, &health));
    EXPECT_TRUE(health.state == NETWORK_STATE_MQTT_READY &&
                health.mqtt_ready != 0u && port.init_calls == 2u &&
                port.connect_calls == 2u);
    return 0;
}

int main(void)
{
    if (test_mqtt_codec() != 0 ||
        test_alarm_priority_and_ack() != 0 ||
        test_retry_and_ota_lease() != 0 ||
        test_disconnect_reinitializes_transport() != 0 ||
        test_network_suspend_resume() != 0 ||
        test_esp8266_raw_tcp() != 0) {
        return 1;
    }
    puts("network_mqtt.host: PASS");
    return 0;
}
