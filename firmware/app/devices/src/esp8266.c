#include "esp8266.h"

#include <stdio.h>
#include <string.h>

#define ESP8266_READ_CHUNK 256u
#define ESP8266_MAX_RESPONSE_READS 12u

static int copy_text(char *destination, size_t capacity, const char *source)
{
    size_t length;

    if (destination == 0 || capacity == 0u || source == 0) {
        return 0;
    }
    length = strlen(source);
    if (length >= capacity) {
        return 0;
    }
    memcpy(destination, source, length + 1u);
    return 1;
}

static size_t find_bytes(const uint8_t *buffer, size_t length,
                         const char *token)
{
    size_t token_length = strlen(token);
    size_t i;

    if (token_length == 0u || token_length > length) {
        return SIZE_MAX;
    }
    for (i = 0u; i <= length - token_length; ++i) {
        if (memcmp(&buffer[i], token, token_length) == 0) {
            return i;
        }
    }
    return SIZE_MAX;
}

static void remove_at_range(esp8266_t *device, size_t start, size_t length)
{
    if (length == 0u || start >= device->at_length ||
        length > device->at_length - start) {
        return;
    }
    memmove(&device->at_buffer[start], &device->at_buffer[start + length],
            device->at_length - start - length);
    device->at_length -= length;
}

static status_t extract_ipd(esp8266_t *device)
{
    static const char prefix[] = "+IPD,";

    for (;;) {
        size_t prefix_index = find_bytes(device->at_buffer,
                                         device->at_length, prefix);
        size_t index;
        size_t payload_length = 0u;
        size_t payload_start;

        if (prefix_index == SIZE_MAX) {
            return SYS_OK;
        }
        index = prefix_index + sizeof(prefix) - 1u;
        if (index >= device->at_length) {
            return ERR_DEVICE_NOT_READY;
        }
        if (device->at_buffer[index] < '0' ||
            device->at_buffer[index] > '9') {
            remove_at_range(device, prefix_index, sizeof(prefix) - 1u);
            device->health.parse_errors++;
            continue;
        }
        while (index < device->at_length &&
               device->at_buffer[index] >= '0' &&
               device->at_buffer[index] <= '9') {
            size_t digit = (size_t)(device->at_buffer[index] - '0');

            if (payload_length > (ESP8266_TCP_BUFFER_SIZE - digit) / 10u) {
                remove_at_range(device, prefix_index,
                                index - prefix_index + 1u);
                device->health.parse_errors++;
                return ERR_NO_MEMORY;
            }
            payload_length = payload_length * 10u + digit;
            index++;
        }
        if (index >= device->at_length) {
            return ERR_DEVICE_NOT_READY;
        }
        if (device->at_buffer[index] != ':') {
            remove_at_range(device, prefix_index,
                            index - prefix_index + 1u);
            device->health.parse_errors++;
            continue;
        }
        payload_start = index + 1u;
        if (payload_length > device->at_length - payload_start) {
            return ERR_DEVICE_NOT_READY;
        }
        if (payload_length > ESP8266_TCP_BUFFER_SIZE - device->tcp_length) {
            device->health.parse_errors++;
            return ERR_QUEUE_FULL;
        }
        memcpy(&device->tcp_buffer[device->tcp_length],
               &device->at_buffer[payload_start], payload_length);
        device->tcp_length += payload_length;
        device->health.tcp_rx_bytes += (uint32_t)payload_length;
        remove_at_range(device, prefix_index,
                        payload_start + payload_length - prefix_index);
    }
}

static status_t ingest_serial(esp8266_t *device, uint32_t timeout_ms)
{
    uint8_t chunk[ESP8266_READ_CHUNK];
    size_t length = 0u;
    status_t status = device->serial_ops->read(
        device->serial_context, chunk, sizeof(chunk), &length, timeout_ms);

    if (status != SYS_OK) {
        if (status != ERR_TIMEOUT) {
            device->health.io_errors++;
        }
        return status;
    }
    if (length == 0u) {
        return ERR_TIMEOUT;
    }
    if (length > ESP8266_AT_BUFFER_SIZE - device->at_length) {
        device->health.parse_errors++;
        return ERR_NO_MEMORY;
    }
    memcpy(&device->at_buffer[device->at_length], chunk, length);
    device->at_length += length;
    status = extract_ipd(device);
    return status == ERR_DEVICE_NOT_READY ? SYS_OK : status;
}

static void consume_token(esp8266_t *device, size_t index,
                          const char *token)
{
    remove_at_range(device, 0u, index + strlen(token));
}

static status_t wait_for_response(esp8266_t *device, const char *success,
                                  const char *alternate,
                                  uint32_t timeout_ms)
{
    uint32_t slice = timeout_ms / ESP8266_MAX_RESPONSE_READS;
    unsigned int attempt;

    if (slice == 0u) {
        slice = 1u;
    }
    for (attempt = 0u; attempt < ESP8266_MAX_RESPONSE_READS; ++attempt) {
        size_t index;
        status_t status;

        (void)extract_ipd(device);
        index = find_bytes(device->at_buffer, device->at_length, success);
        if (index != SIZE_MAX) {
            consume_token(device, index, success);
            return SYS_OK;
        }
        if (alternate != 0) {
            index = find_bytes(device->at_buffer, device->at_length,
                               alternate);
            if (index != SIZE_MAX) {
                consume_token(device, index, alternate);
                return SYS_OK;
            }
        }
        if (find_bytes(device->at_buffer, device->at_length, "ERROR") !=
                SIZE_MAX ||
            find_bytes(device->at_buffer, device->at_length, "FAIL") !=
                SIZE_MAX) {
            device->at_length = 0u;
            return ERR_WIFI;
        }
        status = ingest_serial(device, slice);
        if (status != SYS_OK && status != ERR_TIMEOUT) {
            return status;
        }
    }
    return ERR_TIMEOUT;
}

static status_t write_serial(esp8266_t *device, const uint8_t *data,
                             size_t length, uint32_t timeout_ms)
{
    status_t status = device->serial_ops->write(
        device->serial_context, data, length, timeout_ms);

    if (status != SYS_OK) {
        device->health.io_errors++;
    }
    return status;
}

static status_t send_command(esp8266_t *device, const char *command,
                             const char *success, const char *alternate,
                             uint32_t timeout_ms)
{
    status_t status;

    if (device->serial_ops->flush != 0) {
        (void)device->serial_ops->flush(device->serial_context);
    }
    device->at_length = 0u;
    status = write_serial(device, (const uint8_t *)command,
                          strlen(command), timeout_ms);
    if (status == SYS_OK) {
        device->health.commands++;
        status = wait_for_response(device, success, alternate, timeout_ms);
    }
    device->health.last_error = status;
    return status;
}

status_t esp8266_construct(esp8266_t *device,
                           const esp8266_serial_ops_t *serial_ops,
                           void *serial_context,
                           const esp8266_config_t *config)
{
    if (device == 0 || serial_ops == 0 || serial_context == 0 ||
        config == 0 || serial_ops->init == 0 || serial_ops->write == 0 ||
        serial_ops->read == 0 || config->ssid == 0 ||
        config->ssid[0] == '\0' || config->password == 0 ||
        config->command_timeout_ms == 0u || config->join_timeout_ms == 0u) {
        return ERR_INVALID_ARG;
    }
    memset(device, 0, sizeof(*device));
    if (!copy_text(device->ssid, sizeof(device->ssid), config->ssid) ||
        !copy_text(device->password, sizeof(device->password),
                   config->password)) {
        return ERR_INVALID_ARG;
    }
    device->serial_ops = serial_ops;
    device->serial_context = serial_context;
    device->command_timeout_ms = config->command_timeout_ms;
    device->join_timeout_ms = config->join_timeout_ms;
    device->enable_modem_sleep = config->enable_modem_sleep;
    device->health.last_error = ERR_DEVICE_NOT_READY;
    return SYS_OK;
}

static status_t initialize_module(esp8266_t *device)
{
    char command[128];
    int written;
    status_t status;

    status = send_command(device, "AT\r\n", "OK", 0,
                          device->command_timeout_ms);
    if (status == SYS_OK) {
        status = send_command(device, "ATE0\r\n", "OK", 0,
                              device->command_timeout_ms);
    }
    if (status == SYS_OK) {
        status = send_command(device, "AT+CWMODE=1\r\n", "OK", "no change",
                              device->command_timeout_ms);
    }
    if (status == SYS_OK) {
        written = snprintf(command, sizeof(command),
                           "AT+CWJAP=\"%s\",\"%s\"\r\n",
                           device->ssid, device->password);
        if (written < 0 || (size_t)written >= sizeof(command)) {
            status = ERR_NO_MEMORY;
        } else {
            status = send_command(device, command, "OK", "WIFI GOT IP",
                                  device->join_timeout_ms);
        }
    }
    if (status == SYS_OK) {
        device->health.wifi_joined = 1u;
        status = send_command(device, "AT+CIPMUX=0\r\n", "OK", 0,
                              device->command_timeout_ms);
    }
    if (status == SYS_OK && device->enable_modem_sleep != 0u) {
        status_t sleep_status = send_command(
            device, "AT+SLEEP=2\r\n", "OK", 0,
            device->command_timeout_ms);

        if (sleep_status == SYS_OK) {
            device->health.modem_sleep_enabled = 1u;
        }
    }
    device->health.initialized = status == SYS_OK ? 1u : 0u;
    device->health.last_error = status;
    return status;
}

status_t esp8266_init(esp8266_t *device)
{
    status_t status;

    if (device == 0 || device->serial_ops == 0) {
        return ERR_INVALID_ARG;
    }
    status = device->serial_ops->init(device->serial_context);
    return status == SYS_OK ? initialize_module(device) : status;
}

status_t esp8266_tcp_connect(esp8266_t *device, const char *host,
                             uint16_t port)
{
    char command[128];
    int written;
    status_t status;

    if (device == 0 || host == 0 || host[0] == '\0' || port == 0u ||
        device->health.initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    written = snprintf(command, sizeof(command),
                       "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n",
                       host, (unsigned int)port);
    if (written < 0 || (size_t)written >= sizeof(command)) {
        return ERR_NO_MEMORY;
    }
    status = send_command(device, command, "CONNECT", "ALREADY CONNECTED",
                          device->join_timeout_ms);
    if (status == SYS_OK) {
        device->health.tcp_connected = 1u;
        device->health.tcp_connects++;
    }
    device->health.last_error = status;
    return status;
}

status_t esp8266_tcp_send(esp8266_t *device, const uint8_t *data,
                          size_t length)
{
    char command[32];
    int written;
    status_t status;

    if (device == 0 || data == 0 || length == 0u || length > 2048u ||
        device->health.tcp_connected == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    written = snprintf(command, sizeof(command), "AT+CIPSEND=%u\r\n",
                       (unsigned int)length);
    if (written < 0 || (size_t)written >= sizeof(command)) {
        return ERR_NO_MEMORY;
    }
    status = send_command(device, command, ">", 0,
                          device->command_timeout_ms);
    if (status == SYS_OK) {
        status = write_serial(device, data, length,
                              device->command_timeout_ms);
    }
    if (status == SYS_OK) {
        status = wait_for_response(device, "SEND OK", 0,
                                   device->command_timeout_ms);
    }
    if (status == SYS_OK) {
        device->health.tcp_tx_bytes += (uint32_t)length;
    } else {
        device->health.tcp_connected = 0u;
    }
    device->health.last_error = status;
    return status;
}

status_t esp8266_tcp_receive(esp8266_t *device, uint8_t *data,
                             size_t capacity, size_t *length,
                             uint32_t timeout_ms)
{
    status_t status = SYS_OK;

    if (device == 0 || data == 0 || capacity == 0u || length == 0 ||
        device->health.tcp_connected == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    *length = 0u;
    if (device->tcp_length == 0u) {
        status = ingest_serial(device, timeout_ms);
        if (status != SYS_OK) {
            return status;
        }
    }
    if (find_bytes(device->at_buffer, device->at_length, "CLOSED") !=
        SIZE_MAX) {
        device->health.tcp_connected = 0u;
        device->health.last_error = ERR_IO;
        device->at_length = 0u;
        if (device->tcp_length == 0u) {
            return ERR_IO;
        }
    }
    if (device->tcp_length == 0u) {
        return ERR_TIMEOUT;
    }
    *length = device->tcp_length < capacity ? device->tcp_length : capacity;
    memcpy(data, device->tcp_buffer, *length);
    memmove(device->tcp_buffer, &device->tcp_buffer[*length],
            device->tcp_length - *length);
    device->tcp_length -= *length;
    return SYS_OK;
}

status_t esp8266_tcp_close(esp8266_t *device)
{
    status_t status;

    if (device == 0) {
        return ERR_INVALID_ARG;
    }
    if (device->health.tcp_connected == 0u) {
        return SYS_OK;
    }
    status = send_command(device, "AT+CIPCLOSE\r\n", "OK", "CLOSED",
                          device->command_timeout_ms);
    device->health.tcp_connected = 0u;
    device->health.last_error = status;
    return status;
}

status_t esp8266_suspend(esp8266_t *device)
{
    status_t status;

    if (device == 0 || device->health.initialized == 0u) {
        return ERR_DEVICE_NOT_READY;
    }
    (void)esp8266_tcp_close(device);
    status = device->serial_ops->suspend != 0
        ? device->serial_ops->suspend(device->serial_context) : SYS_OK;
    if (status == SYS_OK) {
        device->health.initialized = 0u;
        device->health.wifi_joined = 0u;
    }
    device->health.last_error = status;
    return status;
}

status_t esp8266_resume(esp8266_t *device)
{
    status_t status;

    if (device == 0) {
        return ERR_INVALID_ARG;
    }
    device->health.reconnects++;
    status = device->serial_ops->resume != 0
        ? device->serial_ops->resume(device->serial_context)
        : device->serial_ops->init(device->serial_context);
    return status == SYS_OK ? initialize_module(device) : status;
}

status_t esp8266_get_health(const esp8266_t *device,
                            esp8266_health_t *health)
{
    if (device == 0 || health == 0) {
        return ERR_INVALID_ARG;
    }
    *health = device->health;
    return SYS_OK;
}

static status_t transport_init(void *context)
{
    return esp8266_init(context);
}

static status_t transport_connect(void *context, const char *host,
                                  uint16_t port)
{
    return esp8266_tcp_connect(context, host, port);
}

static status_t transport_send(void *context, const uint8_t *data,
                               size_t length)
{
    return esp8266_tcp_send(context, data, length);
}

static status_t transport_receive(void *context, uint8_t *data,
                                  size_t capacity, size_t *length,
                                  uint32_t timeout_ms)
{
    return esp8266_tcp_receive(context, data, capacity, length, timeout_ms);
}

static status_t transport_close(void *context)
{
    return esp8266_tcp_close(context);
}

static status_t transport_suspend(void *context)
{
    return esp8266_suspend(context);
}

static status_t transport_resume(void *context)
{
    return esp8266_resume(context);
}

const network_transport_ops_t *esp8266_network_transport_ops(void)
{
    static const network_transport_ops_t ops = {
        transport_init,
        transport_connect,
        transport_send,
        transport_receive,
        transport_close,
        transport_suspend,
        transport_resume
    };

    return &ops;
}
