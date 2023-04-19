#ifndef SIMPLE_SYNC_H
#define SIMPLE_SYNC_H

#define NUM_THREADS 2
#define NUM_QUEUES 256

void sync_produce(unsigned long long elem, int val_id);
unsigned long long sync_consume(int val_id);
void sync_delegate(int tid, void *(*fp)(void *), void *args);
void sync_join();
void sync_init();

#endif
