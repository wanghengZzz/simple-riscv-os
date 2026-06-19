#ifndef TRAP_H_
#define TRAP_H_
#include <stdio.h>

#include "common.h"

__attribute__((noreturn)) void trap_handler(uintptr_t mcause, uintptr_t mepc,
                                            uintptr_t mtval,
                                            trap_frame_t* frame);
void syscall_handler(trap_frame_t* frame);

extern __attribute__((noreturn)) void return_to_user(trap_frame_t* tf);

__attribute__((noreturn)) extern void shced_ecall_timer(trap_frame_t* frame);

__attribute__((noreturn)) extern void timer_ctx_switch(trap_frame_t* frame);

__attribute__((noreturn)) extern void ctx_switch(trap_frame_t* frame);

__attribute__((noreturn)) extern void sched_ecall_block_wait(
    trap_frame_t* frame, int token);

__attribute__((noreturn)) extern void sched_ecall_mutex_lock(
    trap_frame_t* frame, kmutex_t* m);

__attribute__((noreturn)) extern void sched_ecall_mutex_unlock(
    trap_frame_t* frame, kmutex_t* m);

__attribute__((noreturn)) extern void sched_ecall_exit_task();

#endif