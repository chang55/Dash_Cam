#include "frame_queue.h"

static void clear_packet(frame_packet_t *packet)
{
    free(packet->data);
    packet->data = NULL;
    packet->size = 0;
    packet->sequence = 0;
    packet->timestamp = 0;
}

int frame_queue_init(frame_queue_t *queue, uint32_t capacity)
{
    memset(queue, 0, sizeof(*queue));
    queue->items = calloc(capacity, sizeof(*queue->items));
    if (!queue->items) return -1;

    queue->capacity = capacity;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->not_empty, NULL);
    pthread_cond_init(&queue->not_full, NULL);
    return 0;
}

void frame_queue_destroy(frame_queue_t *queue)
{
    if (!queue) return;
    frame_queue_reset(queue);
    free(queue->items);
    queue->items = NULL;
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    pthread_mutex_destroy(&queue->mutex);
}

int frame_queue_push(frame_queue_t *queue, const uint8_t *data, uint32_t size, uint64_t sequence)
{
    if (!queue || !data || size == 0) return -1;

    uint8_t *copy = malloc(size);
    if (!copy) return -1;
    memcpy(copy, data, size);

    pthread_mutex_lock(&queue->mutex);
    while (queue->count == queue->capacity && !queue->stopped) {
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }

    if (queue->stopped) {
        pthread_mutex_unlock(&queue->mutex);
        free(copy);
        return -1;
    }

    frame_packet_t *slot = &queue->items[queue->tail];
    clear_packet(slot);
    slot->data = copy;
    slot->size = size;
    slot->sequence = sequence;
    slot->timestamp = time(NULL);

    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}

int frame_queue_pop(frame_queue_t *queue, frame_packet_t *out)
{
    if (!queue || !out) return -1;

    pthread_mutex_lock(&queue->mutex);
    while (queue->count == 0 && !queue->stopped) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }

    if (queue->count == 0 && queue->stopped) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    *out = queue->items[queue->head];
    memset(&queue->items[queue->head], 0, sizeof(queue->items[queue->head]));
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}

void frame_queue_packet_release(frame_packet_t *packet)
{
    if (!packet) return;
    clear_packet(packet);
}

void frame_queue_stop(frame_queue_t *queue)
{
    pthread_mutex_lock(&queue->mutex);
    queue->stopped = true;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
}

void frame_queue_reset(frame_queue_t *queue)
{
    if (!queue || !queue->items) return;

    pthread_mutex_lock(&queue->mutex);
    for (uint32_t i = 0; i < queue->capacity; i++) {
        clear_packet(&queue->items[i]);
    }
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->stopped = false;
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
}
