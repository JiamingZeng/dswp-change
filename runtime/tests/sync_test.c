#include <stdio.h>
#include <stdlib.h>
#include "simple_sync.h"

void *test_delegate(void *fake_args) {
  unsigned long long *args = (unsigned long long *)fake_args;
  printf("arg0: %lld\n", args[0]);
  printf("arg1: %lld\n", args[1]);
  printf("arg2: %lld\n", args[2]);
  return NULL;
}

int main() {
  unsigned long long args[] = {42, 43, 44};
  sync_delegate(0, test_delegate, args);
  sync_join();
  return 0;
}
