#ifndef GATEWAY_HTTP_CLIENT_RAW_H
#define GATEWAY_HTTP_CLIENT_RAW_H

#include "error_code.h"
#include "network_transport.h"

#include <stddef.h>
#include <stdint.h>

#define HTTP_CLIENT_MAX_HOST_LEN 96u
#define HTTP_CLIENT_MAX_PATH_LEN 160u
#define HTTP_CLIENT_MAX_REQUEST_LEN 320u
#define HTTP_CLIENT_HEADER_BUFFER_LEN 384u

typedef struct {
    uint16_t status_code;
    uint32_t content_length;
} http_header_info_t;

typedef struct {
    uint32_t bytes_total;
    uint32_t bytes_received;
    uint32_t chunk_count;
    status_t last_error;
} http_client_stats_t;

/** HAL-free streaming HTTP client; its owner also owns the transport. */
typedef struct {
    network_transport_t *transport;
    http_header_info_t header;
    http_client_stats_t stats;
    uint32_t receive_timeout_ms;
    uint8_t body_stash[HTTP_CLIENT_HEADER_BUFFER_LEN];
    size_t body_stash_length;
    size_t body_stash_offset;
    uint8_t connected;
} http_client_raw_t;

status_t http_client_construct(http_client_raw_t *client,
                               network_transport_t *transport,
                               uint32_t receive_timeout_ms);
status_t http_parse_url(const char *url, char *out_host, size_t host_size,
                        char *out_path, size_t path_size,
                        uint16_t *out_port);
status_t http_parse_header(const char *header,
                           http_header_info_t *out_info);
status_t http_open_get(http_client_raw_t *client, const char *url);
status_t http_read_body_chunk(http_client_raw_t *client, uint8_t *buffer,
                              size_t buffer_size, size_t *out_length);
status_t http_get_document(http_client_raw_t *client, const char *url,
                           uint8_t *buffer, size_t buffer_size,
                           size_t *out_length);
status_t http_close(http_client_raw_t *client);
status_t http_get_stats(const http_client_raw_t *client,
                        http_client_stats_t *out_stats);

#endif
