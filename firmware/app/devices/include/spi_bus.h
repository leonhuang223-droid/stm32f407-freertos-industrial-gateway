#ifndef GATEWAY_SPI_BUS_H
#define GATEWAY_SPI_BUS_H

#include "error_code.h"

#include <stddef.h>
#include <stdint.h>

typedef struct spi_device spi_device_t;

typedef struct {
    status_t (*select)(void *context, int active);
    status_t (*transfer)(void *context,
                         const uint8_t *tx,
                         uint8_t *rx,
                         size_t length);
    void (*delay_ms)(void *context, uint32_t delay_ms);
} spi_device_ops_t;

struct spi_device {
    const spi_device_ops_t *ops;
    void *context;
    uint32_t timeout_ms;
    uint8_t initialized;
};

status_t spi_device_construct(spi_device_t *device,
                              const spi_device_ops_t *ops,
                              void *context,
                              uint32_t timeout_ms);
status_t spi_device_transfer(spi_device_t *device,
                             const uint8_t *tx,
                             uint8_t *rx,
                             size_t length);
void spi_device_delay(spi_device_t *device, uint32_t delay_ms);

#endif
