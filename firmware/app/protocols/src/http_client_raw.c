#include "http_client_raw.h"

#include <stdio.h>
#include <string.h>

static int ascii_case_equal(char a, char b)
{
    if (a >= 'A' && a <= 'Z') {
        a = (char)(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
        b = (char)(b - 'A' + 'a');
    }
    return a == b;
}

static int starts_with_ci(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (*text == '\0' || !ascii_case_equal(*text, *prefix)) {
            return 0;
        }
        ++text;
        ++prefix;
    }
    return 1;
}

static const char *find_ci(const char *text, const char *needle)
{
    size_t needle_length;

    if (text == 0 || needle == 0) {
        return 0;
    }
    needle_length = strlen(needle);
    while (*text != '\0') {
        size_t index;

        for (index = 0u; index < needle_length; ++index) {
            if (text[index] == '\0' ||
                !ascii_case_equal(text[index], needle[index])) {
                break;
            }
        }
        if (index == needle_length) {
            return text;
        }
        ++text;
    }
    return 0;
}

static status_t set_error(http_client_raw_t *client, status_t status)
{
    if (client != 0) {
        client->stats.last_error = status;
    }
    return status;
}

static status_t copy_part(const char *start, size_t length, char *out,
                          size_t capacity)
{
    if (start == 0 || out == 0 || capacity == 0u || length >= capacity) {
        return ERR_NO_MEMORY;
    }
    memcpy(out, start, length);
    out[length] = '\0';
    return SYS_OK;
}

static status_t parse_port(const char *text, size_t length,
                           uint16_t *out_port)
{
    uint32_t value = 0u;
    size_t index;

    if (text == 0 || length == 0u || out_port == 0) {
        return ERR_INVALID_ARG;
    }
    for (index = 0u; index < length; ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return ERR_HTTP;
        }
        value = value * 10u + (uint32_t)(text[index] - '0');
        if (value > 65535u) {
            return ERR_HTTP;
        }
    }
    if (value == 0u) {
        return ERR_HTTP;
    }
    *out_port = (uint16_t)value;
    return SYS_OK;
}

static status_t parse_content_length(const char *text, uint32_t *out_value)
{
    uint32_t value = 0u;
    uint8_t have_digit = 0u;

    if (text == 0 || out_value == 0) {
        return ERR_INVALID_ARG;
    }
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    while (*text != '\0' && *text != '\r' && *text != '\n') {
        if (*text < '0' || *text > '9') {
            return ERR_HTTP;
        }
        have_digit = 1u;
        if (value > (UINT32_MAX - (uint32_t)(*text - '0')) / 10u) {
            return ERR_HTTP;
        }
        value = value * 10u + (uint32_t)(*text - '0');
        ++text;
    }
    if (have_digit == 0u) {
        return ERR_HTTP;
    }
    *out_value = value;
    return SYS_OK;
}

static const uint8_t *find_header_end(const uint8_t *buffer, size_t length)
{
    size_t index;

    for (index = 0u; buffer != 0 && index + 3u < length; ++index) {
        if (buffer[index] == '\r' && buffer[index + 1u] == '\n' &&
            buffer[index + 2u] == '\r' && buffer[index + 3u] == '\n') {
            return &buffer[index + 4u];
        }
    }
    return 0;
}

static status_t build_request(const char *host, uint16_t port,
                              const char *path, uint8_t *buffer,
                              size_t capacity, size_t *out_length)
{
    char port_text[8];
    int written;

    if (host == 0 || path == 0 || buffer == 0 || out_length == 0) {
        return ERR_INVALID_ARG;
    }
    if (port == 80u) {
        written = snprintf((char *)buffer, capacity,
            "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
            path, host);
    } else {
        (void)snprintf(port_text, sizeof(port_text), ":%u",
                       (unsigned int)port);
        written = snprintf((char *)buffer, capacity,
            "GET %s HTTP/1.1\r\nHost: %s%s\r\nConnection: close\r\n\r\n",
            path, host, port_text);
    }
    if (written < 0 || (size_t)written >= capacity) {
        return ERR_NO_MEMORY;
    }
    *out_length = (size_t)written;
    return SYS_OK;
}

static status_t read_header(http_client_raw_t *client)
{
    uint8_t accumulated[HTTP_CLIENT_HEADER_BUFFER_LEN];
    const uint8_t *body_start = 0;
    size_t accumulated_length = 0u;
    status_t status;

    while (body_start == 0) {
        size_t length = 0u;

        if (accumulated_length == sizeof(accumulated)) {
            return set_error(client, ERR_NO_MEMORY);
        }
        status = network_transport_receive(
            client->transport, &accumulated[accumulated_length],
            sizeof(accumulated) - accumulated_length, &length,
            client->receive_timeout_ms);
        if (status != SYS_OK) {
            return set_error(client, status);
        }
        if (length == 0u) {
            return set_error(client, ERR_HTTP);
        }
        accumulated_length += length;
        body_start = find_header_end(accumulated, accumulated_length);
    }
    {
        size_t header_length = (size_t)(body_start - accumulated);
        size_t body_length = accumulated_length - header_length;
        char header[HTTP_CLIENT_HEADER_BUFFER_LEN];

        if (header_length >= sizeof(header)) {
            return set_error(client, ERR_NO_MEMORY);
        }
        memcpy(header, accumulated, header_length);
        header[header_length] = '\0';
        status = http_parse_header(header, &client->header);
        if (status != SYS_OK) {
            return set_error(client, status);
        }
        if (body_length > (size_t)client->header.content_length) {
            return set_error(client, ERR_HTTP);
        }
        if (body_length != 0u) {
            memcpy(client->body_stash, body_start, body_length);
        }
        client->body_stash_length = body_length;
        client->body_stash_offset = 0u;
        client->stats.bytes_total = client->header.content_length;
    }
    return set_error(client, SYS_OK);
}

status_t http_client_construct(http_client_raw_t *client,
                               network_transport_t *transport,
                               uint32_t receive_timeout_ms)
{
    if (client == 0 || transport == 0 || receive_timeout_ms == 0u) {
        return ERR_INVALID_ARG;
    }
    memset(client, 0, sizeof(*client));
    client->transport = transport;
    client->receive_timeout_ms = receive_timeout_ms;
    client->stats.last_error = SYS_OK;
    return SYS_OK;
}

status_t http_parse_url(const char *url, char *out_host, size_t host_size,
                        char *out_path, size_t path_size,
                        uint16_t *out_port)
{
    const char *host;
    const char *path;
    const char *colon = 0;
    const char *cursor;
    size_t host_length;
    status_t status;

    if (url == 0 || out_host == 0 || out_path == 0 || out_port == 0) {
        return ERR_INVALID_ARG;
    }
    if (starts_with_ci(url, "https://")) {
        return ERR_UNSUPPORTED;
    }
    if (!starts_with_ci(url, "http://")) {
        return ERR_INVALID_ARG;
    }
    host = url + 7u;
    if (*host == '\0' || *host == '/') {
        return ERR_INVALID_ARG;
    }
    path = strchr(host, '/');
    if (path == 0) {
        path = host + strlen(host);
    }
    for (cursor = host; cursor < path; ++cursor) {
        if (*cursor == ':') {
            colon = cursor;
            break;
        }
    }
    host_length = (size_t)((colon != 0 ? colon : path) - host);
    status = copy_part(host, host_length, out_host, host_size);
    if (status != SYS_OK) {
        return status;
    }
    if (colon != 0) {
        status = parse_port(colon + 1u,
                            (size_t)(path - colon - 1u), out_port);
        if (status != SYS_OK) {
            return status;
        }
    } else {
        *out_port = 80u;
    }
    return *path == '\0'
        ? copy_part("/", 1u, out_path, path_size)
        : copy_part(path, strlen(path), out_path, path_size);
}

status_t http_parse_header(const char *header,
                           http_header_info_t *out_info)
{
    const char *line;
    const char *status_text;
    uint8_t have_length = 0u;

    if (header == 0 || out_info == 0) {
        return ERR_INVALID_ARG;
    }
    memset(out_info, 0, sizeof(*out_info));
    if (!starts_with_ci(header, "HTTP/1.0 ") &&
        !starts_with_ci(header, "HTTP/1.1 ")) {
        return ERR_HTTP;
    }
    status_text = header + 9u;
    if (status_text[0] < '0' || status_text[0] > '9' ||
        status_text[1] < '0' || status_text[1] > '9' ||
        status_text[2] < '0' || status_text[2] > '9') {
        return ERR_HTTP;
    }
    out_info->status_code = (uint16_t)(
        (status_text[0] - '0') * 100 +
        (status_text[1] - '0') * 10 + status_text[2] - '0');
    if (out_info->status_code != 200u) {
        return ERR_HTTP;
    }
    if (find_ci(header, "transfer-encoding:") != 0) {
        return ERR_UNSUPPORTED;
    }
    line = header;
    while (line != 0 && *line != '\0') {
        const char *next = strstr(line, "\r\n");

        if (starts_with_ci(line, "Content-Length:")) {
            if (have_length != 0u ||
                parse_content_length(line + 15u,
                                     &out_info->content_length) != SYS_OK) {
                return ERR_HTTP;
            }
            have_length = 1u;
        }
        line = next != 0 ? next + 2u : 0;
    }
    return have_length != 0u ? SYS_OK : ERR_HTTP;
}

status_t http_open_get(http_client_raw_t *client, const char *url)
{
    char host[HTTP_CLIENT_MAX_HOST_LEN];
    char path[HTTP_CLIENT_MAX_PATH_LEN];
    uint8_t request[HTTP_CLIENT_MAX_REQUEST_LEN];
    uint16_t port;
    size_t request_length = 0u;
    status_t status;

    if (client == 0 || client->transport == 0 || url == 0) {
        return ERR_INVALID_ARG;
    }
    memset(&client->header, 0, sizeof(client->header));
    memset(&client->stats, 0, sizeof(client->stats));
    client->body_stash_length = 0u;
    client->body_stash_offset = 0u;
    status = http_parse_url(url, host, sizeof(host), path, sizeof(path),
                            &port);
    if (status == SYS_OK) {
        status = build_request(host, port, path, request, sizeof(request),
                               &request_length);
    }
    if (status == SYS_OK) {
        status = network_transport_connect(client->transport, host, port);
    }
    if (status != SYS_OK) {
        return set_error(client, status);
    }
    client->connected = 1u;
    status = network_transport_send(client->transport, request,
                                    request_length);
    if (status == SYS_OK) {
        status = read_header(client);
    }
    if (status != SYS_OK) {
        (void)http_close(client);
    }
    return set_error(client, status);
}

status_t http_read_body_chunk(http_client_raw_t *client, uint8_t *buffer,
                              size_t buffer_size, size_t *out_length)
{
    uint32_t remaining;
    size_t length = 0u;
    status_t status;

    if (out_length != 0) {
        *out_length = 0u;
    }
    if (client == 0 || buffer == 0 || buffer_size == 0u ||
        out_length == 0 || client->connected == 0u) {
        return ERR_INVALID_ARG;
    }
    if (client->stats.bytes_received >= client->header.content_length) {
        return SYS_OK;
    }
    remaining = client->header.content_length - client->stats.bytes_received;
    if (client->body_stash_offset < client->body_stash_length) {
        size_t stashed = client->body_stash_length -
                         client->body_stash_offset;

        length = stashed < buffer_size ? stashed : buffer_size;
        if (length > (size_t)remaining) {
            return set_error(client, ERR_HTTP);
        }
        memcpy(buffer, &client->body_stash[client->body_stash_offset],
               length);
        client->body_stash_offset += length;
    } else {
        size_t capacity = (size_t)remaining < buffer_size
            ? (size_t)remaining : buffer_size;

        status = network_transport_receive(client->transport, buffer,
            capacity, &length, client->receive_timeout_ms);
        if (status != SYS_OK) {
            return set_error(client, status);
        }
        if (length == 0u || length > capacity) {
            return set_error(client, ERR_HTTP);
        }
    }
    client->stats.bytes_received += (uint32_t)length;
    client->stats.chunk_count++;
    *out_length = length;
    return set_error(client, SYS_OK);
}

status_t http_get_document(http_client_raw_t *client, const char *url,
                           uint8_t *buffer, size_t buffer_size,
                           size_t *out_length)
{
    status_t status;

    if (out_length != 0) {
        *out_length = 0u;
    }
    if (client == 0 || url == 0 || buffer == 0 || out_length == 0) {
        return ERR_INVALID_ARG;
    }
    status = http_open_get(client, url);
    if (status != SYS_OK) {
        return status;
    }
    if (client->header.content_length > buffer_size) {
        (void)http_close(client);
        return set_error(client, ERR_NO_MEMORY);
    }
    while (client->stats.bytes_received < client->header.content_length) {
        size_t length = 0u;

        status = http_read_body_chunk(
            client, &buffer[client->stats.bytes_received],
            buffer_size - client->stats.bytes_received, &length);
        if (status != SYS_OK || length == 0u) {
            (void)http_close(client);
            return set_error(client, status != SYS_OK ? status : ERR_HTTP);
        }
    }
    *out_length = client->stats.bytes_received;
    status = http_close(client);
    return set_error(client, status);
}

status_t http_close(http_client_raw_t *client)
{
    status_t status;

    if (client == 0 || client->transport == 0) {
        return ERR_INVALID_ARG;
    }
    status = client->connected != 0u
        ? network_transport_close(client->transport) : SYS_OK;
    client->connected = 0u;
    client->body_stash_length = 0u;
    client->body_stash_offset = 0u;
    return status;
}

status_t http_get_stats(const http_client_raw_t *client,
                        http_client_stats_t *out_stats)
{
    if (client == 0 || out_stats == 0) {
        return ERR_INVALID_ARG;
    }
    *out_stats = client->stats;
    return SYS_OK;
}
