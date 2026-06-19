#ifndef MUTEX_H_
#define MUTEX_H_

#include "common.h"
#include "spinlock.h"

#define KMUTEX_INIT   \
  {.locked = 0,       \
   .owner = nullptr,  \
   .waiter_head = 0,  \
   .waiter_tail = 0,  \
   .waiter_count = 0, \
   .guard = SPINLOCK_INIT}

static inline void mutex_lock(kmutex_t* m) {
  int old;
  __asm__ volatile("amoswap.w.aq %0, %2, (%1)"
                   : "=r"(old)
                   : "r"(&m->locked), "r"(1)
                   : "memory");

  if (old == 0) return;

  register uintptr_t a0 __asm__("a0") = (uintptr_t)m;
  register uintptr_t a7 __asm__("a7") = ECALL_MUTEX_LOCK;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

static inline void mutex_unlock(kmutex_t* m) {
  register uintptr_t a0 __asm__("a0") = (uintptr_t)m;
  register uintptr_t a7 __asm__("a7") = ECALL_MUTEX_UNLOCK;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

#endif