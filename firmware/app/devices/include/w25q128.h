#ifndef GATEWAY_W25Q128_H
#define GATEWAY_W25Q128_H

#include "external_flash_layout.h"
#include "spi_bus.h"
#include "storage_media.h"

#include <stddef.h>
#include <stdint.h>

typedef struct w25q128 w25q128_t;

typedef struct {
    status_t (*init)(w25q128_t *device);
    status_t (*read)(w25q128_t *device,
                     uint32_t address,
                     uint8_t *buffer,
                     size_t length);
    status_t (*program)(w25q128_t *device,
                        uint32_t address,
                        const uint8_t *data,
                        size_t length);
    status_t (*erase_sector)(w25q128_t *device, uint32_t address);
    status_t (*power_down)(w25q128_t *device);
    status_t (*wake)(w25q128_t *device);
} w25q128_ops_t;

typedef struct {
    uint32_t expected_jedec_id;
    uint32_t total_size;
    uint32_t operation_timeout_ms;
} w25q128_config_t;

typedef struct {
    uint32_t detected_jedec_id;
    uint32_t reads;
    uint32_t programs;
    uint32_t erases;
    uint32_t failures;
    status_t last_error;
    uint8_t initialized;
    uint8_t powered_down;
} w25q128_health_t;

struct w25q128 {
    const w25q128_ops_t *ops;
    spi_device_t *spi;
    w25q128_config_t config;
    w25q128_health_t health;
};

status_t w25q128_construct(w25q128_t *device,
                           spi_device_t *spi,
                           const w25q128_config_t *config);
status_t w25q128_init(w25q128_t *device);
status_t w25q128_read(w25q128_t *device,
                      uint32_t address,
                      uint8_t *buffer,
                      size_t length);
status_t w25q128_program(w25q128_t *device,
                         uint32_t address,
                         const uint8_t *data,
                         size_t length);
status_t w25q128_erase_sector(w25q128_t *device, uint32_t address);
status_t w25q128_power_down(w25q128_t *device);
status_t w25q128_wake(w25q128_t *device);
status_t w25q128_get_health(const w25q128_t *device, w25q128_health_t *health);
const storage_media_ops_t *w25q128_storage_media_ops(void);

#endif
