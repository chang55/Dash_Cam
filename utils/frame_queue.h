#ifndef DVR_FRAME_QUEUE_H
#define DVR_FRAME_QUEUE_H

#include "common.h"

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint64_t sequence;
    time_t timestamp;
} frame_packet_t;

typedef struct {
    frame_packet_t *items;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    bool stopped;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} frame_queue_t;

int frame_queue_init(frame_queue_t *queue, uint32_t capacity);
void frame_queue_destroy(frame_queue_t *queue);
int frame_queue_push(frame_queue_t *queue, const uint8_t *data, uint32_t size, uint64_t sequence);
int frame_queue_pop(frame_queue_t *queue, frame_packet_t *out);
void frame_queue_packet_release(frame_packet_t *packet);
void frame_queue_stop(frame_queue_t *queue);
void frame_queue_reset(frame_queue_t *queue);

#endif
