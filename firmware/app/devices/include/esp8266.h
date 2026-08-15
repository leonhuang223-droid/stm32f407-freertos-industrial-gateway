#ifndef GATEWAY_ESP8266_H
#define GATEWAY_ESP8266_H

#include "network_transport.h"

#include <stddef.h>
#include <stdint.h>

#define ESP8266_SSID_MAX 32u
#define ESP8266_PASSWORD_MAX 64u
#define ESP8266_AT_BUFFER_SIZE 768u
#define ESP8266_TCP_BUFFER_SIZE 1024u

typedef struct {
    status_t (*init)(void *context);
    status_t (*write)(void *context, const uint8_t *data, size_t length,
                      uint32_t timeout_ms);
    status_t (*read)(void *context, uint8_t *data, size_t capacity,
                     size_t *length, uint32_t timeout_ms);
    status_t (*flush)(void *context);
    status_t (*suspend)(void *context);
    status_t (*resume)(void *context);
} esp8266_serial_ops_t;

typedef struct {
    const char *ssid;
    const char *password;
    uint32_t command_timeout_ms;
    uint32_t join_timeout_ms;
    uint8_t enable_modem_sleep;
} esp8266_config_t;

typedef struct {
    uint32_t commands;
    uint32_t tcp_connects;
    uint32_t tcp_tx_bytes;
    uint32_t tcp_rx_bytes;
    uint32_t reconnects;
    uint32_t parse_errors;
    uint32_t io_errors;
    status_t last_error;
    uint8_t initialized;
    uint8_t wifi_joined;
    uint8_t tcp_connected;
    uint8_t modem_sleep_enabled;
} esp8266_health_t;

typedef struct {
    const esp8266_serial_ops_t *serial_ops;
    void *serial_context;
    char ssid[ESP8266_SSID_MAX + 1u];
    char password[ESP8266_PASSWORD_MAX + 1u];
    uint32_t command_timeout_ms;
    uint32_t join_timeout_ms;
    uint8_t enable_modem_sleep;
    uint8_t at_buffer[ESP8266_AT_BUFFER_SIZE];
    size_t at_length;
    uint8_t tcp_buffer[ESP8266_TCP_BUFFER_SIZE];
    size_t tcp_length;
    esp8266_health_t health;
} esp8266_t;

status_t esp8266_construct(esp8266_t *device,
                           const esp8266_serial_ops_t *serial_ops,
                           void *serial_context,
                           const esp8266_config_t *config);
status_t esp8266_init(esp8266_t *device);
status_t esp8266_tcp_connect(esp8266_t *device, const char *host,
                             uint16_t port);
status_t esp8266_tcp_send(esp8266_t *device, const uint8_t *data,
                          size_t length);
status_t esp8266_tcp_receive(esp8266_t *device, uint8_t *data,
                             size_t capacity, size_t *length,
                             uint32_t timeout_ms);
status_t esp8266_tcp_close(esp8266_t *device);
status_t esp8266_suspend(esp8266_t *device);
status_t esp8266_resume(esp8266_t *device);
status_t esp8266_get_health(const esp8266_t *device,
                            esp8266_health_t *health);
const network_transport_ops_t *esp8266_network_transport_ops(void);

#endif
