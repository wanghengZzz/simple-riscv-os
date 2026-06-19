#ifndef SCHED_H_
#define SCHED_H_
#include <stdlib.h>

#include "clint.h"
#include "common.h"
#include "utils.h"

task_t* create_task(void (*entry)(uintptr_t), int priority, uintptr_t hartid);
void kill_task(task_t* t);
void enqueue(task_t* t);
void remove_from_queue(task_t* t);
void wake_sleeping_tasks(void);
uintptr_t* schedule(trap_frame_t* frame);

static inline void exit_task(void) {
  register uintptr_t a7 __asm__("a7") = ECALL_EXIT;
  __asm__ volatile("ecall" : : "r"(a7) : "memory");
}

__attribute__((noreturn)) void wake_specific_task(trap_frame_t* frame,
                                                  task_t* t);

__attribute__((noreturn)) void shced_ecall_timer(trap_frame_t* frame);

__attribute__((noreturn)) void sched_ecall_block_wait(trap_frame_t* frame,
                                                      int token);

__attribute__((noreturn)) void sched_ecall_mutex_lock(trap_frame_t* frame,
                                                      kmutex_t* m);

__attribute__((noreturn)) void sched_ecall_mutex_unlock(trap_frame_t* frame,
                                                        kmutex_t* m);

__attribute__((noreturn)) void sched_ecall_exit_task();

__attribute__((noreturn)) void timer_ctx_switch(trap_frame_t* frame);

__attribute__((noreturn)) void ctx_switch(trap_frame_t* frame);

extern __attribute__((noreturn)) void return_to_user(trap_frame_t* tf);

#endif