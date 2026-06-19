#ifndef UTILS_H_
#define UTILS_H_

#include <stddef.h>
#include <stdio.h>

#define nullptr ((void*)0)

#define CHECK_ERR(cond)   \
  do {                    \
    if (!(cond)) {        \
      exit(EXIT_FAILURE); \
    }                     \
  } while (0);

#define CHECK_ALLOC_ERR_SAFE(ptr) CHECK_ERR(((void*){0} = (ptr)))

void* memdup(void* src, size_t sz);
void* malloc_safe(size_t sz);
void* calloc_safe(size_t n, size_t type_size);

#endif