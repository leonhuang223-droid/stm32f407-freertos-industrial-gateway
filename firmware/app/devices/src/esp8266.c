#include "esp8266.h"

#include <stdio.h>
#include <string.h>

#define ESP8266_READ_CHUNK 256u
#define ESP8266_WAIT_SLICE_MS 100u

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

static size_t find_response(const uint8_t *buffer, size_t length, const char *token)
{
    size_t offset = 0u;
    while (offset < length) {
        size_t index = find_bytes(buffer + offset, length - offset, token);
        if (index == SIZE_MAX) {
            return SIZE_MAX;
        }
        index += offset;
        if (token[0] == '>' || index == 0u ||
            buffer[index - 1u] == '\r' || buffer[index - 1u] == '\n') {
            return index;
        }
        offset = index + 1u;
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

/* Discard consumed AT chatter without discarding a fragmented stream marker.
 * Receive-only HTTP streams may contain thousands of short +IPD frames. */
static void retain_stream_marker(esp8266_t *device, int retain_close)
{
    size_t start = find_bytes(device->at_buffer, device->at_length, "+IPD,");
    size_t keep = 0u;
    size_t count;
    if (start != SIZE_MAX) {
        remove_at_range(device, 0u, start);
        return;
    }
    for (count = 1u; count <= 4u && count <= device->at_length; ++count) {
        if (memcmp(device->at_buffer + device->at_length - count, "+IPD,", count) == 0) {
            keep = count;
        }
    }
    if (retain_close) {
        for (count = 1u; count <= 7u && count <= device->at_length; ++count) {
            if (memcmp(device->at_buffer + device->at_length - count, "CLOSED\r\n", count) == 0 && count > keep) {
                keep = count;
            }
        }
    }
    if (keep != 0u) {
        memmove(device->at_buffer, device->at_buffer + device->at_length - keep, keep);
    }
    device->at_length = keep;
}

/* AT text and binary payload have separate storage. A frame may be larger
 * than either buffer; tcp_receive drains it as a stream. */
static status_t extract_ipd(esp8266_t *device)
{
    size_t start = find_bytes(device->at_buffer, device->at_length, "+IPD,");
    size_t index;
    size_t length = 0u;

    if (start == SIZE_MAX) {
        return SYS_OK;
    }
    index = start + 5u;
    if (index == device->at_length) {
        return SYS_OK;
    }
    if (device->at_buffer[index] < '0' || device->at_buffer[index] > '9') {
        return ERR_PROTOCOL;
    }
    while (index < device->at_length &&
           device->at_buffer[index] >= '0' && device->at_buffer[index] <= '9') {
        size_t digit = device->at_buffer[index++] - '0';
        if (length > (UINT32_MAX - digit) / 10u) {
            return ERR_PROTOCOL;
        }
        length = length * 10u + digit;
    }
    if (index == device->at_length) {
        return SYS_OK;
    }
    if (device->at_buffer[index] != ':') {
        return ERR_PROTOCOL;
    }
    remove_at_range(device, start, index + 1u - start);
    device->ipd_remaining = length;
    return SYS_OK;
}

static status_t ingest_serial(esp8266_t *device, uint32_t timeout_ms)
{
    uint8_t chunk[ESP8266_READ_CHUNK];
    size_t length = 0u;
    size_t index;
    size_t capacity = ESP8266_TCP_BUFFER_SIZE - device->tcp_length;
    status_t status;

    if (capacity == 0u) {
        return ERR_QUEUE_FULL;
    }
    if (capacity > sizeof(chunk)) {
        capacity = sizeof(chunk);
    }
    status = device->serial_ops->read(device->serial_context, chunk,
                                      capacity, &length, timeout_ms);
    if (status != SYS_OK) {
        if (status != ERR_TIMEOUT) {
            device->health.io_errors++;
        }
        return status;
    }
    if (length == 0u) {
        return ERR_TIMEOUT;
    }
    if (length > capacity) {
        return ERR_PROTOCOL;
    }
    for (index = 0u; index < length; ++index) {
        if (device->ipd_remaining != 0u) {
            device->tcp_buffer[device->tcp_length++] = chunk[index];
            device->ipd_remaining--;
            device->health.tcp_rx_bytes++;
        } else {
            if (device->at_length == sizeof(device->at_buffer)) {
                device->health.parse_errors++;
                return ERR_NO_MEMORY;
            }
            device->at_buffer[device->at_length++] = chunk[index];
            status = extract_ipd(device);
            if (status != SYS_OK) {
                device->health.parse_errors++;
                return status;
            }
            if (find_response(device->at_buffer, device->at_length, "CLOSED\r\n") != SIZE_MAX) {
                device->health.tcp_connected = 0u;
                device->health.last_error = ERR_IO;
            }
        }
    }
    return SYS_OK;
}

static uint32_t clock_ms(const esp8266_t *device)
{
    return device->serial_ops->now_ms != 0
        ? device->serial_ops->now_ms(device->serial_context) : 0u;
}

static uint32_t remaining_ms(const esp8266_t *device, uint32_t start,
                             uint32_t budget, uint32_t fallback_elapsed)
{
    uint32_t elapsed = device->serial_ops->now_ms != 0
        ? clock_ms(device) - start : fallback_elapsed;
    return elapsed < budget ? budget - elapsed : 0u;
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
    uint32_t start = clock_ms(device);
    uint32_t elapsed = 0u;

    for (;;) {
        size_t index;
        status_t status;

        uint32_t remaining;
        uint32_t slice;

        index = find_response(device->at_buffer, device->at_length, success);
        if (index != SIZE_MAX) {
            consume_token(device, index, success);
            return SYS_OK;
        }
        if (alternate != 0) {
            index = find_response(device->at_buffer, device->at_length,
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
        remaining = remaining_ms(device, start, timeout_ms, elapsed);
        if (remaining == 0u) {
            return ERR_TIMEOUT;
        }
        slice = remaining < ESP8266_WAIT_SLICE_MS
            ? remaining : ESP8266_WAIT_SLICE_MS;
        status = ingest_serial(device, slice);
        elapsed += status == ERR_TIMEOUT ? slice : 1u;
        if (status != SYS_OK && status != ERR_TIMEOUT) {
            return status;
        }
    }
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

    /* Preserve unsolicited +IPD received during command transmission. */
    retain_stream_marker(device, 0);
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
        config->command_timeout_ms == 0u || config->join_timeout_ms == 0u ||
        config->command_timeout_ms > INT32_MAX || config->join_timeout_ms > INT32_MAX) {
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

    if (device->needs_reset != 0u) {
        status = send_command(device, "AT+RST\r\n", "ready", 0, 3000u);
        if (status != SYS_OK) {
            return status;
        }
        device->needs_reset = 0u;
    }
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
            status = send_command(device, command, "OK", 0,
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
    device->tcp_length = 0u;
    device->ipd_remaining = 0u;
    device->at_length = 0u;
    if (device->serial_ops->flush != 0) {
        (void)device->serial_ops->flush(device->serial_context);
    }
    written = snprintf(command, sizeof(command),
                       "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n",
                       host, (unsigned int)port);
    if (written < 0 || (size_t)written >= sizeof(command)) {
        return ERR_NO_MEMORY;
    }
    status = send_command(device, command, "CONNECT\r\n", "ALREADY CONNECTED\r\n",
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
    uint32_t start;
    uint32_t elapsed = 0u;

    if (length != 0) {
        *length = 0u;
    }
    if (device == 0 || data == 0 || capacity == 0u || length == 0 ||
        timeout_ms == 0u) {
        return ERR_INVALID_ARG;
    }
    start = clock_ms(device);
    for (;;) {
        uint32_t remaining;
        uint32_t slice;
        status_t status;

        if (find_bytes(device->at_buffer, device->at_length, "CLOSED") !=
            SIZE_MAX) {
            device->health.tcp_connected = 0u;
            device->health.last_error = ERR_IO;
            device->at_length = 0u;
        }
        retain_stream_marker(device, 1);
        /* Buffered bytes remain readable after the peer closes. */
        if (device->tcp_length != 0u) {
            break;
        }
        if (device->health.tcp_connected == 0u) {
            return ERR_IO;
        }
        remaining = remaining_ms(device, start, timeout_ms, elapsed);
        if (remaining == 0u) {
            return ERR_TIMEOUT;
        }
        slice = remaining < ESP8266_WAIT_SLICE_MS
            ? remaining : ESP8266_WAIT_SLICE_MS;
        status = ingest_serial(device, slice);
        elapsed += status == ERR_TIMEOUT ? slice : 1u;
        if (status != SYS_OK && status != ERR_TIMEOUT) {
            return status;
        }
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
    device->tcp_length = 0u;
    device->ipd_remaining = 0u;
    device->at_length = 0u;
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

    if (device == 0) {
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

/* Cooperative connection commands: each poll reads at most one 256-byte chunk
 * and waits at most 10 ms. Join/TCP deadlines span polls rather than task waits. */
static status_t command_step(esp8266_t *device, const char *command,
    const char *success, const char *alternate, uint32_t now_ms,
    uint32_t timeout_ms)
{
    status_t status;
    size_t index;
    if (device->command_pending == 0u) {
        device->at_length = 0u;
        status = write_serial(device, (const uint8_t *)command, strlen(command), 100u);
        if (status != SYS_OK) {
            return status;
        }
        device->command_deadline_ms = now_ms + timeout_ms;
        device->command_pending = 1u;
        device->health.commands++;
        return ERR_IN_PROGRESS;
    }
    if ((int32_t)(now_ms - device->command_deadline_ms) >= 0) {
        device->command_pending = 0u;
        return ERR_TIMEOUT;
    }
    status = ingest_serial(device, 10u);
    if (status != SYS_OK && status != ERR_TIMEOUT) {
        device->command_pending = 0u;
        return status;
    }
    index = find_response(device->at_buffer, device->at_length, success);
    if (index != SIZE_MAX) {
        consume_token(device, index, success);
        device->command_pending = 0u;
        return SYS_OK;
    }
    if (alternate != 0) {
        index = find_response(device->at_buffer, device->at_length, alternate);
        if (index != SIZE_MAX) {
            consume_token(device, index, alternate);
            device->command_pending = 0u;
            return SYS_OK;
        }
    }
    if (find_bytes(device->at_buffer, device->at_length, "ERROR") != SIZE_MAX ||
        find_bytes(device->at_buffer, device->at_length, "FAIL") != SIZE_MAX) {
        device->command_pending = 0u;
        return ERR_WIFI;
    }
    if ((int32_t)(now_ms - device->command_deadline_ms) >= 0) {
        device->command_pending = 0u;
        return ERR_TIMEOUT;
    }
    return ERR_IN_PROGRESS;
}

static status_t module_init_step(void *opaque, uint32_t now_ms)
{
    esp8266_t *device = opaque;
    const char *command = 0;
    const char *success = "OK";
    char join[128];
    uint32_t timeout = device->command_timeout_ms;
    status_t status;
    if (device->health.initialized != 0u) {
        return SYS_OK;
    }
    if (device->init_phase == 0u) {
        status = device->serial_ops->init(device->serial_context);
        if (status != SYS_OK) {
            return status;
        }
        device->init_phase = device->needs_reset != 0u ? 1u : 2u;
    }
    switch (device->init_phase) {
    case 1u: command = "AT+RST\r\n"; success = "ready"; timeout = 3000u; break;
    case 2u: command = "AT\r\n"; break;
    case 3u: command = "ATE0\r\n"; break;
    case 4u: command = "AT+CWMODE=1\r\n"; break;
    case 5u:
        if (snprintf(join, sizeof(join), "AT+CWJAP=\"%s\",\"%s\"\r\n",
                     device->ssid, device->password) >= (int)sizeof(join)) {
            return ERR_NO_MEMORY;
        }
        command = join;
        timeout = device->join_timeout_ms;
        break;
    case 6u: command = "AT+CIPMUX=0\r\n"; break;
    case 7u: command = "AT+SLEEP=2\r\n"; break;
    default: return ERR_PROTOCOL;
    }
    status = command_step(device, command, success,
        device->init_phase == 4u ? "no change" : 0, now_ms, timeout);
    if (status == ERR_IN_PROGRESS) {
        return status;
    }
    if (status != SYS_OK && device->init_phase != 7u) {
        device->health.last_error = status;
        device->needs_reset = 1u;
        device->init_phase = 0u;
        return status;
    }
    if (device->init_phase == 1u) {
        device->needs_reset = 0u;
    } else if (device->init_phase == 5u) {
        device->health.wifi_joined = 1u;
    } else if (device->init_phase == 7u) {
        device->health.modem_sleep_enabled = status == SYS_OK ? 1u : 0u;
    }
    device->init_phase++;
    if (device->init_phase == 8u ||
        (device->init_phase == 7u && device->enable_modem_sleep == 0u)) {
        device->init_phase = 0u;
        device->health.initialized = 1u;
        device->health.last_error = SYS_OK;
        return SYS_OK;
    }
    return ERR_IN_PROGRESS;
}

static status_t tcp_connect_step(void *opaque, const char *host,
                                  uint16_t port, uint32_t now_ms)
{
    esp8266_t *device = opaque;
    char command[128];
    status_t status;
    if (host == 0 || port == 0u || device->health.initialized == 0u) {
        return ERR_INVALID_ARG;
    }
    if (device->command_pending == 0u) {
        device->tcp_length = 0u;
        device->ipd_remaining = 0u;
    }
    if (snprintf(command, sizeof(command), "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n",
                 host, (unsigned int)port) >= (int)sizeof(command)) {
        return ERR_NO_MEMORY;
    }
    status = command_step(device, command, "CONNECT\r\n", "ALREADY CONNECTED\r\n",
                           now_ms, device->join_timeout_ms);
    if (status == SYS_OK) {
        device->health.tcp_connected = 1u;
        device->health.tcp_connects++;
    } else if (status != ERR_IN_PROGRESS) {
        device->needs_reset = 1u;
    }
    device->health.last_error = status;
    return status;
}

static void cancel_connect(void *opaque)
{
    esp8266_t *device = opaque;
    if (device->command_pending != 0u || device->init_phase != 0u) {
        device->needs_reset = 1u;
        device->command_pending = 0u;
        device->init_phase = 0u;
        device->health.initialized = 0u;
        device->health.wifi_joined = 0u;
        device->health.tcp_connected = 0u;
        device->at_length = 0u;
        device->tcp_length = 0u;
        device->ipd_remaining = 0u;
    }
}

const network_connect_step_ops_t *esp8266_connect_step_ops(void)
{
    static const network_connect_step_ops_t ops = {
        module_init_step, tcp_connect_step, cancel_connect
    };
    return &ops;
}
