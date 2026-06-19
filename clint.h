#ifndef CLINT_H_
#define CLINT_H_

#include "common.h"

#define CLINT_BASE 0x02000000UL

#define CLINT_MTIME (CLINT_BASE + 0xBFF8)
#define CLINT_MTIMECMP_BASE (CLINT_BASE + 0x4000)
#define CLINT_MTIMECMP(n) MEM_ACCESS(CLINT_MTIMECMP_BASE + ((n) << 3))

/* TIMEBASE_FREQ = 10 MHz */
#define TIMEBASE_FREQ 10000000UL

/*

    IPI（Inter-Processor Interrupt) via CLINT MSIP
    CLINT MSIP register: hart N in 0x02000000 + N*4

*/

#define CLINT_MSIP_BASE 0x02000000UL
#define CLINT_MSIP(n) MEM_ACCESS(CLINT_MSIP_BASE + ((n) << 2))

#define WAIT_TIME_MS(ms) (clint_get_time() + (TIMEBASE_FREQ / 1000) * ms)
#define DELAY_CYCLES (6e5 * 1UL)

static inline riscv_time_t clint_get_time(void) {
  return MEM_ACCESS(CLINT_MTIME);
}

static inline void clint_set_timecmp(uintptr_t hartid, riscv_time_t deadline) {
  CLINT_MTIMECMP(hartid) = deadline;
}

static inline void clint_arm_timer(uintptr_t hartid, riscv_time_t delta) {
  clint_set_timecmp(hartid, clint_get_time() + delta);
}

static inline void clint_disarm_timer(uintptr_t hartid) {
  CLINT_MTIMECMP(hartid) = UINT64_MAX;
}

static inline void clint_arm_timer_ms(uint32_t hartid, riscv_time_t ms) {
  clint_arm_timer(hartid, (TIMEBASE_FREQ / 1000) * ms);
}

void clint_delay_ms(riscv_time_t ms);

static inline void task_sleep_ms(riscv_time_t ms) {
  register riscv_time_t a0 __asm__("a0") = ms;
  register riscv_time_t a7 __asm__("a7") = ECALL_SLEEP_MS;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

#endif