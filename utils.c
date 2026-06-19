#include "utils.h"

#include <stdlib.h>
#include <string.h>

__attribute__((noreturn)) void _exit(int status) {
  (void)status;
  for (;;) __asm__ volatile("wfi");
}

void *memdup(void *src, size_t sz) {
  void *data = malloc_safe(sz);
  return data ? memcpy(data, src, sz) : nullptr;
}

void *calloc_safe(size_t n, size_t type_size) {
  void *mem = calloc(n, type_size);
  CHECK_ALLOC_ERR_SAFE(mem);
  return mem;
}

void *malloc_safe(size_t sz) {
  void *mem = malloc(sz);
  CHECK_ALLOC_ERR_SAFE(mem);
  return mem;
}