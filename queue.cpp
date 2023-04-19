#include <stdlib.h>
#include "queue.h"

void queue_init(queue_t *q) {
  q->head = 0;
  q->tail = 0;
  q->size = 0;
  pthread_mutex_init(&(q->mutex), NULL);
  pthread_cond_init(&(q->empty_cvar), NULL);
  pthread_cond_init(&(q->full_cvar), NULL);
}

void queue_destroy(queue_t *q) {
  pthread_mutex_destroy(&(q->mutex));
  pthread_cond_destroy(&(q->empty_cvar));
  pthread_cond_destroy(&(q->full_cvar));
}

void queue_push(queue_t *q, unsigned long long elem) {
  pthread_mutex_lock(&(q->mutex));
  while (q->size == QUEUE_MAXLEN) {
    pthread_cond_wait(&(q->full_cvar), &(q->mutex));
  }

  q->arr[q->tail] = elem;
  q->tail = (q->tail + 1) % QUEUE_MAXLEN;
  q->size += 1;
  pthread_cond_signal(&(q->empty_cvar));
  pthread_mutex_unlock(&(q->mutex));
}

unsigned long long queue_pop(queue_t *q) {
  pthread_mutex_lock(&(q->mutex));
  while (q->size == 0) {
    pthread_cond_wait(&(q->empty_cvar), &(q->mutex));
  }

  unsigned long long ret_val = q->arr[q->head];
  q->head = (q->head + 1) % QUEUE_MAXLEN;
  q->size -= 1;
  pthread_cond_signal(&(q->full_cvar));
  pthread_mutex_unlock(&(q->mutex));

  return ret_val;
}
