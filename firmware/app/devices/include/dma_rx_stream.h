#ifndef GATEWAY_DMA_RX_STREAM_H
#define GATEWAY_DMA_RX_STREAM_H

#include <stddef.h>
#include <stdint.h>

/* Single ISR producer, single task consumer. The task masks the producer IRQ
 * while draining/resetting. Keep the DMA endpoint (including size == capacity)
 * so duplicate IDLE/TC callbacks cannot replay a completed DMA block. */
typedef struct {
    const uint8_t *dma;
    size_t dma_capacity;
    uint8_t *ring;
    size_t ring_capacity;
    size_t dma_position;
    volatile size_t write_position;
    volatile size_t read_position;
    volatile uint8_t overflow;
} dma_rx_stream_t;

static inline void dma_rx_stream_reset(dma_rx_stream_t *stream)
{
    stream->dma_position = 0u;
    stream->write_position = 0u;
    stream->read_position = 0u;
    stream->overflow = 0u;
}

static inline void dma_rx_stream_publish(dma_rx_stream_t *stream, size_t end)
{
    size_t position = stream->dma_position;
    if (end > stream->dma_capacity || end == position) {
        return;
    }
    while (position != end) {
        size_t next;
        if (position == stream->dma_capacity) {
            position = 0u;
            if (end == 0u) {
                break;
            }
        }
        next = (stream->write_position + 1u) % stream->ring_capacity;
        if (next == stream->read_position) {
            stream->overflow = 1u;
        } else {
            stream->ring[stream->write_position] = stream->dma[position];
            stream->write_position = next;
        }
        position++;
    }
    stream->dma_position = end;
}

static inline size_t dma_rx_stream_read(dma_rx_stream_t *stream,
                                        uint8_t *data, size_t capacity)
{
    size_t length = 0u;
    while (length < capacity &&
           stream->read_position != stream->write_position) {
        data[length++] = stream->ring[stream->read_position];
        stream->read_position = (stream->read_position + 1u) %
                                stream->ring_capacity;
    }
    return length;
}

#endif
