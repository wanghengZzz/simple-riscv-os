

#include "clint.h"
#include "common.h"
#include "plic.h"
#include "sched.h"
#include "smp.h"
#include "trap.h"
#include "uart.h"
#include "virtio.h"

extern char _bss_start[];
extern char _bss_end[];
extern char _stack_top[];

volatile uintptr_t _main_finish = 0;
extern scheduler_t scheduler;
#define NUM_SECTORS 10
task_t* io_task[MAX_HARTS] = {};
uint8_t blk_buf[MAX_HARTS][512 * NUM_SECTORS];

int fib(int a) {
  if (a <= 1) return a;
  if (a == 2) return 1;
  return fib(a - 1) + fib(a - 2);
}

void task_a(uintptr_t hartid) {
  for (;;) {
    uart_printf("[hart %lu] [task A] running\n", hartid);
    task_sleep_ms(1e3);
  }
}

void task_b(uintptr_t hartid) {
  for (;;) {
    uart_printf("[hart %lu] [task B] running\n", hartid);
    task_sleep_ms(1e3);
  }
}

void task_c(uintptr_t hartid) {
  for (;;) {
    uart_printf("[hart %lu] [task C] running\n", hartid);
    uart_printf("fib: %d\n", fib(35));
    task_sleep_ms(1e3);
  }
}

void task_d(uintptr_t hartid) {
  for (;;) {
    uart_printf("[hart %lu] [task D] running\n", hartid);
    uart_printf("fib: %d\n", fib(35));
    task_sleep_ms(1e3);
  }
}

void test_io(uintptr_t hartid) {
  for (;;) {
    char str[512 + 1] = {};
    for (size_t i = 0; i < 100; ++i) str[i] = blk_buf[hartid][i + 100 * hartid];
    uart_printf("hartid: %d\n%s\n", hartid, str);
    task_sleep_ms(1e3);
  }
}

void blk_read_async_task(uintptr_t hartid) {
  blk_read_sync(0, blk_buf[hartid], NUM_SECTORS);

  if (io_task[hartid]) io_task[hartid]->wake_time = 0;

  exit_task();
}

void welcome(uintptr_t hartid) {
  uart_printf("[hart %lu] online, stack @ \n", hartid);
}

static inline void timer_init(uintptr_t hartid, uint64_t ms) {
  clint_arm_timer_ms(hartid, ms);
  __asm__ volatile("csrs mie,     %0" ::"r"(MIE_MTIE));
}

static inline void enable_all_interrupts(uintptr_t hartid, uint64_t ms) {
  clint_arm_timer_ms(hartid, ms);
  /* MATE */
  __asm__ volatile("csrs mie,     %0" ::"r"(MIE_MATE));
  /* MIE */
  __asm__ volatile("csrs mstatus, %0" ::"r"(MSTATUS_MIE));
}

void minor_main(uintptr_t hartid, uintptr_t dtb) {
  /* init mmio device and open PLIC */
  int blk_slot = virtio_blk_slot();
  CHECK_ERR(blk_slot != -1);
  plic_init(hartid);

  /* kick external device to put data into blk buffer */
  task_t* tk = create_task(blk_read_async_task, 1, hartid);
  enqueue(tk);

  /* create task for testing received data from external io */
  task_t* tio = create_task(test_io, 1, hartid);
  tio->wake_time = UINT64_MAX;
  enqueue(tio);
  io_task[hartid] = tio;

  /* test multiple harts */
  task_t* ta = create_task(task_a, 1, hartid);
  task_t* tb = create_task(task_b, 1, hartid);
  task_t* tc = create_task(task_c, 1, hartid);
  task_t* td = create_task(task_d, 1, hartid);
  enqueue(ta);
  enqueue(tb);
  enqueue(tc);
  enqueue(td);
  scheduler.harts_current[hartid] = ta;
  scheduler.harts_current[hartid]->state = TASK_RUNNING;

  __asm__ volatile("csrw mscratch, %0" ::"r"(
      scheduler.harts_current[hartid]->kernel_stack_top));

  while (1) {
    int mmio_init_flag = 1;
    for (size_t i = 0; i < MAX_HARTS; ++i)
      if (!scheduler.harts_current[i]) mmio_init_flag = 0;
    if (mmio_init_flag) break;
  }

  /* timer interrupt init */
  timer_init(hartid, 1e3);

  write_mie(MIE_MEIE);
  write_mstatus(MSTATUS_MIE);

  /* context switch to current user task */
  return_to_user(scheduler.harts_current[hartid]->sp);
}

void boot_main(uintptr_t hartid, uintptr_t dtb) {
  /* init uart */
  uart_init();

  /* kick external device to put data into the blk buffer */
  task_t* tk = create_task(blk_read_async_task, 1, hartid);
  enqueue(tk);

  /* init mmio device and open PLIC */
  int blk_slot = virtio_blk_slot();
  CHECK_ERR(blk_slot != -1);
  plic_init(hartid);
  virtio_blk_init(blk_slot);

  /* create task for testing received data from external io */
  task_t* tio = create_task(test_io, 1, hartid);
  tio->wake_time = UINT64_MAX;
  enqueue(tio);
  io_task[hartid] = tio;

  /* test multiple harts */
  task_t* ta = create_task(task_a, 1, hartid);
  task_t* tb = create_task(task_b, 1, hartid);
  task_t* tc = create_task(task_c, 1, hartid);
  task_t* td = create_task(task_d, 1, hartid);
  enqueue(ta);
  enqueue(tb);
  enqueue(tc);
  enqueue(td);
  scheduler.harts_current[hartid] = ta;
  scheduler.harts_current[hartid]->state = TASK_RUNNING;
  __asm__ volatile("csrw mscratch, %0" ::"r"(
      scheduler.harts_current[hartid]->kernel_stack_top));

  /* minor main can start to work */
  _main_finish = 1;

  while (1) {
    int mmio_init_flag = 1;
    for (size_t i = 0; i < MAX_HARTS; ++i)
      if (!scheduler.harts_current[i]) mmio_init_flag = 0;
    if (mmio_init_flag) break;
  }

  /* timer interrupt init */
  timer_init(hartid, 1e3);

  write_mie(MIE_MEIE);
  write_mstatus(MSTATUS_MIE);

  /* context switch to current user task */
  return_to_user(scheduler.harts_current[hartid]->sp);
}
