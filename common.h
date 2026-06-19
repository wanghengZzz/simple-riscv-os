#ifndef COMMON_H_
#define COMMON_H_

#include "types.h"

typedef struct {
  uintptr_t ra;
  uintptr_t sp;
  uintptr_t gp;
  uintptr_t tp;
  uintptr_t t0;
  uintptr_t t1;
  uintptr_t t2;
  uintptr_t s0;
  uintptr_t s1;
  uintptr_t a0;
  uintptr_t a1;
  uintptr_t a2;
  uintptr_t a3;
  uintptr_t a4;
  uintptr_t a5;
  uintptr_t a6;
  uintptr_t a7;
  uintptr_t s2;
  uintptr_t s3;
  uintptr_t s4;
  uintptr_t s5;
  uintptr_t s6;
  uintptr_t s7;
  uintptr_t s8;
  uintptr_t s9;
  uintptr_t s10;
  uintptr_t s11;
  uintptr_t t3;
  uintptr_t t4;
  uintptr_t t5;
  uintptr_t t6;
  uintptr_t epc;
} trap_frame_t;

#define MCAUSE_IS_INTERRUPT(cause) ((cause) >> (UL_NUM_BITS - 1))
#define MCAUSE_CODE(cause) ((cause) & ~(1UL << (UL_NUM_BITS - 1)))

#define EXC_MISALIGNED_FETCH 0
#define EXC_FAULT_FETCH 1
#define EXC_ILLEGAL_INST 2
#define EXC_BREAKPOINT 3
#define EXC_MISALIGNED_LOAD 4
#define EXC_FAULT_LOAD 5
#define EXC_MISALIGNED_STORE 6
#define EXC_FAULT_STORE 7
#define EXC_ECALL_U 8
#define EXC_ECALL_M 11

#define INT_M_SOFTWARE 3
#define INT_M_TIMER 7
#define INT_M_EXTERNAL 11

#define MSTATUS_MPP_MASK (3UL << 11)
#define MSTATUS_MPP_M (3UL << 11)
#define MSTATUS_MPP_U (0UL << 11)
#define MSTATUS_MPIE (1UL << 7)
#define MSTATUS_MIE (1UL << 3)

#define MIE_MSIE (1UL << 3)
#define MIE_MTIE (1UL << 7)
#define MIE_MEIE (1UL << 11)

/* enable all interrupts */
#define MIE_MATE (MIE_MSIE | MIE_MTIE | MIE_MEIE)

#define SYSCON_BASE 0x00100000UL
#define SYSCON_POWEROFF 0x5555
#define SYSCON_REBOOT 0x7777

#define MAX_TASK 128
#define MAX_STACK_SIZE 2048

#define TIMER_INT_MS 100

#define ECALL_SLEEP_MS 1
#define ECALL_BLOCK_WAIT 2   /* a0 = virtio inflight token */
#define ECALL_MUTEX_LOCK 3   /* a0 = kmutex_t* */
#define ECALL_MUTEX_UNLOCK 4 /* a0 = kmutex_t* */
#define ECALL_EXIT 5

#define MAX_HARTS 4

typedef enum {
  TASK_READY = 0,
  TASK_RUNNING = 1,
  TASK_SLEEPING = 2,
  TASK_DEAD = 3,
} task_state_t;

typedef struct task_t task_t;

struct task_t {
  uintptr_t* user_stack;
  uintptr_t* kernel_stack;
  uintptr_t kernel_stack_top;
  trap_frame_t* sp;
  uintptr_t wake_time;
  int id;
  int priority;
  task_state_t state;
  task_t* next;
  /* virtio inflight token (-1 = not blocking on IO) */
  int block_token;
};

typedef struct {
  task_t* harts_current[MAX_HARTS];
  task_t* harts_ready[MAX_HARTS];
} scheduler_t;

typedef struct {
  volatile lock_size_t lock;
} spinlock_t;

typedef struct {
  volatile int locked;
  task_t* owner;
  task_t* waiters[MAX_TASK];
  int waiter_head;
  int waiter_tail;
  int waiter_count;
  spinlock_t guard;
} kmutex_t;

inline void write_mie(unsigned long reg) {
  __asm__ volatile("csrs mie,     %0" ::"r"(reg) : "memory");
}

inline void write_mstatus(unsigned long reg) {
  __asm__ volatile("csrs mstatus, %0" ::"r"(reg) : "memory");
}
#endif
