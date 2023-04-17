/*
 * A thread-safe FIFO queue implementation with a fixed-length circular buffer
 * for the queue.
 */
#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>

#define QUEUE_MAXLEN 32

typedef struct {
  unsigned long long arr[QUEUE_MAXLEN];
  int head;
  int tail;
  int size;
  pthread_mutex_t mutex;
  pthread_cond_t empty_cvar;
  pthread_cond_t full_cvar;
} queue_t;

void queue_init(queue_t *q);
void queue_destroy(queue_t *q);

/*
 * Pushes the specified element onto the end of the queue.  Blocks if the queue
 * is full.
 */
void queue_push(queue_t *q, unsigned long long elem);

/*
 * Pops the next element off the head of the queue.  Blocks if the queue is
 * empty.
 */
unsigned long long queue_pop(queue_t *q);

#endif
