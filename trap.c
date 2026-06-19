#include "trap.h"

#include "clint.h"
#include "plic.h"
#include "smp.h"
#include "uart.h"
#include "virtio.h"

static const char* exc_name[] = {
    [0] = "Instruction Address Misaligned",
    [1] = "Instruction Access Fault",
    [2] = "Illegal Instruction",
    [3] = "Breakpoint",
    [4] = "Load Address Misaligned",
    [5] = "Load Access Fault",
    [6] = "Store/AMO Address Misaligned",
    [7] = "Store/AMO Access Fault",
    [8] = "Ecall from U-mode",
    [9] = "Ecall from S-mode",
    [10] = "Reserved",
    [11] = "Ecall from M-mode",
    [12] = "Instruction Page Fault",
    [13] = "Load Page Fault",
    [14] = "Reserved",
    [15] = "Store/AMO Page Fault",
};

extern task_t* io_task[MAX_HARTS];
extern uintptr_t* wake_specific_task(trap_frame_t* frame, task_t* t);
extern scheduler_t scheduler;

__attribute__((noreturn)) static void handle_interrupt(uintptr_t code,
                                                       uintptr_t mepc,
                                                       trap_frame_t* frame) {
  uintptr_t hartid = smp_hartid();

  switch (code) {
    case INT_M_TIMER:
      timer_ctx_switch(frame);
      break;

    case INT_M_EXTERNAL: {
      uint32_t source = PLIC_M_CLAIM(hartid);
      if (source == 0) break;
      if (source >= VIRTIO_IRQ && source < VIRTIO_IRQ + VIRTIO_COUNT) {
        virtio_blk_wake_blocked_tasks();
      }
      PLIC_M_COMPLETE(hartid) = source;
      break;
    }

    case INT_M_SOFTWARE:
      smp_clear_ipi();
      ctx_switch(frame);
      break;

    default:
      break;
  }
  __asm__ volatile("csrw mscratch, %0" ::"r"(
      scheduler.harts_current[hartid]->kernel_stack_top));
  return_to_user(scheduler.harts_current[hartid]->sp);
}

static void poweroff(void) {
  uart_puts("\n[boot] powering off...\n");
  clint_delay_ms(500);
  REG32(SYSCON_BASE) = SYSCON_POWEROFF;
}

static void handle_exception(uintptr_t code, uintptr_t mepc, uintptr_t mtval,
                             trap_frame_t* frame) {
  uintptr_t hartid = smp_hartid();
  const char* name = (code < 16) ? exc_name[code] : "Unknown";

  uart_printf("\n[trap] hart %u EXCEPTION: %s\n", hartid, name);
  uart_printf("  mepc  = 0x%lx\n", mepc);
  uart_printf("  mtval = 0x%lx\n", mtval);
  uart_printf("  ra    = 0x%lx\n", frame->ra);
  uart_printf("  sp    = 0x%lx\n", frame->sp);
  uart_printf("  a0    = 0x%lx\n", frame->a0);

  poweroff();
}

void syscall_handler(trap_frame_t* frame) {
  frame->epc += 4;
  uintptr_t code = frame->a7;
  switch (frame->a7) {
    case ECALL_SLEEP_MS:
      shced_ecall_timer(frame);
      break;
    case ECALL_BLOCK_WAIT:
      sched_ecall_block_wait(frame, (int)frame->a0);
      break;
    case ECALL_MUTEX_LOCK:
      sched_ecall_mutex_lock(frame, (kmutex_t*)frame->a0);
      break;
    case ECALL_MUTEX_UNLOCK:
      sched_ecall_mutex_unlock(frame, (kmutex_t*)frame->a0);
      break;
    case ECALL_EXIT:
      sched_ecall_exit_task(frame);
      break;
    default:
      uart_printf("[syscall] unknown syscall %lu\n", code);
      frame->a0 = (uintptr_t)-1;
      break;
  }
}

__attribute__((noreturn)) void trap_handler(uintptr_t mcause, uintptr_t mepc,
                                            uintptr_t mtval,
                                            trap_frame_t* frame) {
  if (MCAUSE_IS_INTERRUPT(mcause))
    handle_interrupt(MCAUSE_CODE(mcause), mepc, frame);

  uintptr_t code = MCAUSE_CODE(mcause);
  switch (code) {
    case EXC_ECALL_U:
      syscall_handler(frame);
      break;
    case EXC_ECALL_M:
      syscall_handler(frame);
      break;
    default:
      handle_exception(MCAUSE_CODE(mcause), mepc, mtval, frame);
  };
  return_to_user(frame);
}
