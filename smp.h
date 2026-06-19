#ifndef SMP_H_
#define SMP_H_

#include "clint.h"
#include "common.h"
#include "spinlock.h"

static inline uintptr_t smp_hartid(void) {
  uintptr_t id;
  __asm__ volatile("csrr %0, mhartid" : "=r"(id));
  return id;
}

static inline void smp_send_ipi(uintptr_t target_hart) {
  CLINT_MSIP(target_hart) = 1;
}

static inline void smp_clear_ipi(void) { CLINT_MSIP(smp_hartid()) = 0; }

extern volatile uintptr_t _init_finish;

__attribute__((weak)) void minor_main(uintptr_t hartid, uintptr_t dtb);

void smp_wake_all_harts(void);

#endif