#ifndef W25Q_BOOT_H
#define W25Q_BOOT_H

#include "error_code.h"
#include "external_flash_layout.h"

#include <stddef.h>
#include <stdint.h>

#define W25Q128_TOTAL_SIZE EXTERNAL_FLASH_TOTAL_SIZE
#define W25Q128_JEDEC_ID EXTERNAL_FLASH_JEDEC_ID

typedef struct {
    status_t (*select)(void *context, int active);
    status_t (*transmit)(void *context, const uint8_t *data, size_t length);
    status_t (*receive)(void *context, uint8_t *data, size_t length);
    void (*delay_ms)(void *context, uint32_t delay_ms);
    void *context;
} w25q_boot_port_t;

typedef struct {
    uint32_t expected_jedec_id;
    uint32_t total_size;
} w25q_boot_config_t;

typedef struct {
    w25q_boot_port_t port;
    w25q_boot_config_t config;
    uint32_t detected_jedec_id;
    uint8_t initialized;
} w25q_boot_t;

status_t w25q_boot_init(w25q_boot_t *device,
                        const w25q_boot_config_t *config,
                        const w25q_boot_port_t *port);
status_t w25q_boot_read(w25q_boot_t *device,
                        uint32_t address,
                        uint8_t *buffer,
                        size_t length);
uint32_t w25q_boot_detected_jedec_id(const w25q_boot_t *device);

#endif
