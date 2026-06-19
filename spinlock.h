#ifndef SPINLOCK_H_
#define SPINLOCK_H_

#include "common.h"
#include "riscv-reg.h"
#include "types.h"
// #include <stdatomic.h>

static inline void close_interrupt(void) {
  __asm__ volatile("csrc mstatus, %0" ::"r"(MSTATUS_MIE) : "memory");
}

static inline void open_interrupt(void) {
  __asm__ volatile("csrs mstatus, %0" ::"r"(MSTATUS_MIE) : "memory");
}

#define SPINLOCK_INIT ((spinlock_t){.lock = 0})

static inline void spin_lock(spinlock_t* s) {
  lock_size_t tmp;
  lock_size_t token = 1;
  __asm__ volatile(
      "1:\n"
#if (__riscv_xlen == 64)
      "   lr.d    %0, (%1)       \n"
      "   bnez    %0, 1b         \n"
      "   sc.d    %0, %2, (%1)   \n"
#else
      "   lr.w    %0, (%1)       \n"
      "   bnez    %0, 1b         \n"
      "   sc.w    %0, %2, (%1)   \n"
#endif
      "   bnez    %0, 1b         \n"
      "   fence   r, rw          \n"
      : "=&r"(tmp)
      : "r"(&s->lock), "r"(token)
      : "memory");
}

static inline int spin_trylock(spinlock_t* s) {
  lock_size_t old;

  __asm__ volatile(
      "li             t0, 1           \n"
      "amoswap.w.aq   %0, t0, (%1)    \n"
      : "=&r"(old)
      : "r"(&s->lock)
      : "memory", "t0");
  return old == 0;
}

static inline void spin_unlock(spinlock_t* s) {
  /* unblocking */

  __asm__ volatile(
      "fence  rw, w       \n"
#if (__riscv_xlen == 64)
      "sd     zero, 0(%0) \n"
#else
      "sw     zero, 0(%0) \n"
#endif
      :
      : "r"(&s->lock)
      : "memory");
}

static inline int spin_is_locked(const spinlock_t* s) {
  lock_size_t v;
  __asm__ volatile(
#if (__riscv_xlen == 32)
      "lw   %0, 0(%1)   \n"
#else
      "ld   %0, 0(%1)   \n"
#endif
      : "=r"(v)
      : "r"(&s->lock));
  return v != 0;
}

#endif
