#include <pthread.h>
#include <stdio.h>
#include "queue.h"
#include "simple_sync.h"

static pthread_t threads[NUM_THREADS] = {};
static queue_t data_queues[NUM_QUEUES] = {};


void sync_produce(unsigned long long elem, int val_id) {
	//printf("%lld\n", elem);
	//printf("produce in %d\n", val_id);
	queue_push(&data_queues[val_id], elem);
}

unsigned long long sync_consume(int val_id) {
//	unsigned long long res = queue_pop(&data_queues[val_id]);
// printf("consume in %d\n", val_id);
//	printf("%lld\n", res);
//	return res;
	return queue_pop(&data_queues[val_id]);
}

// called by master thread
// sends function pointers to the auxiliary threads
void sync_delegate(int tid, void *(*fp)(void *), void *args) {
	printf("create thread!\n");
	pthread_create(&threads[tid], NULL, fp, args);
}

void sync_join() {
	printf("join\n");
	int i;
	for (i = 0; i < NUM_THREADS; i++) {
		if (threads[i]) {
			pthread_join(threads[i], NULL);
		}
	}
}

void sync_init() {
	printf("init\n");
	int i;
	for (i = 0; i < NUM_QUEUES; i++) {
		queue_init(&data_queues[i]);
	}
}

