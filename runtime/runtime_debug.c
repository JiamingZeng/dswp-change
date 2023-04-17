#include <stdio.h>
#include "runtime_debug.h"

void showPlace() {
	printf("I'm here!\n");
}

void showPtr(void *elem) {
	printf("show pointer: %p\n", elem);
}

void showValue(unsigned long long elem) {
	printf("show value: %lld \t %d\n", elem, *((int *) elem));
}
