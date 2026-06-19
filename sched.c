#include "sched.h"

#include "smp.h"
#include "spinlock.h"
#include "virtio.h"

scheduler_t scheduler = {};

static task_t* ready_queue[MAX_HARTS] = {};
static int next_id = 0;
static spinlock_t task_alloc_lock = SPINLOCK_INIT;
static task_t* deferred_free_task[MAX_HARTS] = {};

task_t* create_task(void (*entry)(uintptr_t), int priority, uintptr_t hartid) {
  if (priority < 0 || priority > 7) return nullptr;

  spin_lock(&task_alloc_lock);
  if (next_id >= MAX_TASK) {
    spin_unlock(&task_alloc_lock);
    return nullptr;
  }

  task_t* t = calloc_safe(1, sizeof(task_t));

  /* user stack */
  t->user_stack = calloc_safe(MAX_STACK_SIZE, sizeof(uintptr_t));
  uintptr_t user_sp = (uintptr_t)(t->user_stack + MAX_STACK_SIZE);
  user_sp &= ~0xfUL;

  /* kernel stack */
  t->kernel_stack = calloc_safe(MAX_STACK_SIZE, sizeof(uintptr_t));
  t->kernel_stack_top = (uintptr_t)(t->kernel_stack + MAX_STACK_SIZE);

  trap_frame_t* frame = (trap_frame_t*)t->kernel_stack_top - 1;

  frame->epc = (uintptr_t)entry;
  frame->sp = user_sp;
  frame->a0 = hartid;

  uintptr_t gp_val;
  __asm__ volatile("mv %0, gp" : "=r"(gp_val));
  frame->gp = gp_val;

  t->sp = frame;
  t->id = next_id++;
  t->priority = priority;
  t->state = TASK_SLEEPING;
  t->next = nullptr;

  spin_unlock(&task_alloc_lock);
  return t;
}

void kill_task(task_t* t) {
  if (!t) return;
  free(t->kernel_stack);
  free(t->user_stack);
  free(t);
}

void enqueue(task_t* t) {
  uintptr_t hartid = smp_hartid();
  task_t** queue = &ready_queue[hartid];

  if (!*queue) {
    t->next = t;
    *queue = t;
    return;
  }
  t->next = (*queue)->next;
  (*queue)->next = t;
}

void remove_from_queue(task_t* t) {
  uintptr_t hartid = smp_hartid();
  task_t** queue = &ready_queue[hartid];

  if (!*queue || !t) return;

  task_t* prev = *queue;
  while (prev->next != t) {
    prev = prev->next;
    if (prev == *queue) return;
  }

  if (t->next == t) {
    *queue = nullptr;
    t->next = nullptr;
    return;
  }

  prev->next = t->next;
  if (*queue == t) *queue = prev;

  t->next = nullptr;
}

uintptr_t* schedule(trap_frame_t* frame) {
  uintptr_t hartid = smp_hartid();
  if (!scheduler.harts_current[hartid] || !ready_queue[hartid]) return nullptr;

  scheduler.harts_current[hartid]->sp = frame;
  if (scheduler.harts_current[hartid]->state == TASK_RUNNING)
    scheduler.harts_current[hartid]->state = TASK_SLEEPING;

  scheduler.harts_current[hartid]->wake_time = WAIT_TIME_MS(1);
  task_t* next = scheduler.harts_current[hartid];
  if (scheduler.harts_ready[hartid] &&
      scheduler.harts_ready[hartid]->state == TASK_READY) {
    scheduler.harts_current[hartid] = scheduler.harts_ready[hartid];
  } else {
    do {
      next = next->next;
    } while (next->state != TASK_READY &&
             next != scheduler.harts_current[hartid]);
    scheduler.harts_current[hartid] = next;
  }
  scheduler.harts_ready[hartid] = nullptr;
  scheduler.harts_current[hartid]->state = TASK_RUNNING;
  __asm__ volatile("csrw mscratch, %0" ::"r"(
      scheduler.harts_current[hartid]->kernel_stack_top));
  return (uintptr_t*)scheduler.harts_current[hartid]->sp;
}

__attribute__((noreturn)) void wake_specific_task(trap_frame_t* frame,
                                                  task_t* t) {
  uintptr_t hartid = smp_hartid();

  if (!t) ctx_switch(frame);

  scheduler.harts_current[hartid]->sp = frame;
  if (scheduler.harts_current[hartid]->state == TASK_RUNNING)
    scheduler.harts_current[hartid]->state = TASK_SLEEPING;

  scheduler.harts_current[hartid] = t;
  t->state = TASK_RUNNING;
  __asm__ volatile("csrw mscratch, %0" ::"r"(t->kernel_stack_top));
  return_to_user(t->sp);
}

void wake_sleeping_tasks(void) {
  uintptr_t hartid = smp_hartid();
  if (!ready_queue[hartid] ||
      (scheduler.harts_ready[hartid] &&
       scheduler.harts_ready[hartid]->state == TASK_READY))
    return;

  task_t* candidate = nullptr;
  task_t* t = scheduler.harts_current[hartid]->next;
  uintptr_t now = clint_get_time();
  do {
    if ((!candidate || t->priority > candidate->priority) &&
        t->state == TASK_SLEEPING && now >= t->wake_time) {
      candidate = t;
    }
    t = t->next;
  } while (t != scheduler.harts_current[hartid]);

  if (!candidate) return;
  candidate->state = TASK_READY;
  scheduler.harts_ready[hartid] = candidate;
}

__attribute__((noreturn)) void shced_ecall_timer(trap_frame_t* frame) {
  uintptr_t hartid = smp_hartid();
  riscv_time_t ms = frame->a0;
  task_t* cur = scheduler.harts_current[hartid];
  task_t* next = cur;
  task_t* candidate = nullptr;
  task_t* earliest = cur;
  cur->sp = frame;
  cur->state = TASK_SLEEPING;
  cur->wake_time = WAIT_TIME_MS(ms);

  do {
    uintptr_t now = clint_get_time();
    next = next->next;
    if (next->state == TASK_READY)
      break;
    else if (next->state == TASK_SLEEPING && now >= next->wake_time &&
             ((!candidate) || next->priority > candidate->priority)) {
      candidate = next;
    }
    if (next->state == TASK_SLEEPING && next->wake_time < earliest->wake_time)
      earliest = next;
  } while (next->state != TASK_READY && next != cur);

  if (next->state == TASK_READY)
    scheduler.harts_current[hartid] = next;
  else if (candidate)
    scheduler.harts_current[hartid] = candidate;
  else {
    while (clint_get_time() < earliest->wake_time);
    scheduler.harts_current[hartid] = earliest;
  }

  scheduler.harts_current[hartid]->state = TASK_RUNNING;
  __asm__ volatile("csrw mscratch, %0" ::"r"(
      scheduler.harts_current[hartid]->kernel_stack_top));
  return_to_user(scheduler.harts_current[hartid]->sp);
}

__attribute__((noreturn)) void timer_ctx_switch(trap_frame_t* frame) {
  wake_sleeping_tasks();
  trap_frame_t* next = (trap_frame_t*)schedule(frame);
  clint_arm_timer_ms(smp_hartid(), 1000UL);
  return_to_user(next ? next : frame);
}

__attribute__((noreturn)) void ctx_switch(trap_frame_t* frame) {
  wake_sleeping_tasks();
  trap_frame_t* next = (trap_frame_t*)schedule(frame);
  return_to_user(next ? next : frame);
}

__attribute__((noreturn)) void sched_ecall_block_wait(trap_frame_t* frame,
                                                      int token) {
  uintptr_t hartid = smp_hartid();
  task_t* cur = scheduler.harts_current[hartid];

  cur->block_token = token;
  cur->sp = frame;
  cur->state = TASK_SLEEPING;
  cur->wake_time = UINT64_MAX;

  virtio_blk_set_waiter(token, cur);

  if (virtio_blk_is_done(token)) {
    cur->state = TASK_READY;
    cur->block_token = -1;
  }

  task_t* next = cur;
  do {
    next = next->next;
  } while (next->state != TASK_READY && next != cur);

  if (next->state != TASK_READY) {
    cur->state = TASK_RUNNING;
    __asm__ volatile("csrw mscratch, %0" ::"r"(cur->kernel_stack_top));
    return_to_user(frame);
  }

  scheduler.harts_current[hartid] = next;
  next->state = TASK_RUNNING;
  __asm__ volatile("csrw mscratch, %0" ::"r"(next->kernel_stack_top));
  return_to_user(next->sp);
}

__attribute__((noreturn)) void sched_ecall_mutex_lock(trap_frame_t* frame,
                                                      kmutex_t* m) {
  uintptr_t hartid = smp_hartid();
  task_t* cur = scheduler.harts_current[hartid];

  /* 先檢查鎖，ecall 途中可能已被釋放 */
  spin_lock(&m->guard);
  if (!m->locked) {
    m->locked = 1;
    m->owner = cur;
    spin_unlock(&m->guard);
    __asm__ volatile("csrw mscratch, %0" ::"r"(cur->kernel_stack_top));
    return_to_user(frame);
  }
  spin_unlock(&m->guard);

  /* 找下一個 READY task */
  task_t* next = cur;
  do {
    next = next->next;
  } while (next->state != TASK_READY && next != cur);

  if (!ready_queue[hartid] || next == cur) {
    /* 沒有其他 task，只能 spin 等，這個 hart 上只有一個 task
     * 且持有鎖的是別的 hart，靠 spin 等對方 unlock */
    while (1) {
      spin_lock(&m->guard);
      if (!m->locked) {
        m->locked = 1;
        m->owner = cur;
        spin_unlock(&m->guard);
        break;
      }
      spin_unlock(&m->guard);
    }
    __asm__ volatile("csrw mscratch, %0" ::"r"(cur->kernel_stack_top));
    return_to_user(frame);
  }

  /* 有其他 task，把自己加進 waiters 然後切走 */
  // cur->sp       = frame;
  cur->state = TASK_SLEEPING;
  cur->wake_time = UINT64_MAX;

  spin_lock(&m->guard);
  m->waiters[m->waiter_tail] = cur;
  m->waiter_tail = (m->waiter_tail + 1) % MAX_TASK;
  m->waiter_count++;
  spin_unlock(&m->guard);

  scheduler.harts_current[hartid] = next;
  next->state = TASK_RUNNING;
  __asm__ volatile("csrw mscratch, %0" ::"r"(next->kernel_stack_top));
  return_to_user(next->sp);
}

__attribute__((noreturn)) void sched_ecall_mutex_unlock(trap_frame_t* frame,
                                                        kmutex_t* m) {
  uintptr_t hartid = smp_hartid();
  task_t* cur = scheduler.harts_current[hartid];
  spin_lock(&m->guard);
  if (m->waiter_count > 0) {
    task_t* waiter = m->waiters[m->waiter_head];
    m->waiter_head = (m->waiter_head + 1) % MAX_TASK;
    m->waiter_count--;
    m->owner = waiter;
    /* locked 保持 1，直接轉交給 waiter，避免競爭視窗 */
    spin_unlock(&m->guard);

    waiter->state = TASK_READY;
    __asm__ volatile("csrw mscratch, %0" ::"r"(cur->kernel_stack_top));
    return_to_user(frame);
  }

  m->owner = nullptr;
  m->locked = 0;
  spin_unlock(&m->guard);
  __asm__ volatile("csrw mscratch, %0" ::"r"(cur->kernel_stack_top));
  return_to_user(frame);
}

__attribute__((noreturn)) void sched_ecall_exit_task() {
  uintptr_t hartid = smp_hartid();
  task_t* cur = scheduler.harts_current[hartid];
  task_t* candidate = nullptr;
  task_t* start = cur->next;

  if (deferred_free_task[hartid]) {
    kill_task(deferred_free_task[hartid]);
    deferred_free_task[hartid] = nullptr;
  }

  remove_from_queue(cur);
  deferred_free_task[hartid] = cur;

  if (scheduler.harts_ready[hartid]) {
    candidate = scheduler.harts_ready[hartid];
  } else {
    while (1) {
      task_t* next = start;
      uintptr_t now = clint_get_time();
      do {
        if ((!candidate || next->priority > candidate->priority) &&
            next->state == TASK_SLEEPING && now >= next->wake_time) {
          candidate = next;
        }
        next = next->next;
      } while (next != start);
      if (candidate) break;
    }
  }
  scheduler.harts_ready[hartid] = nullptr;
  scheduler.harts_current[hartid] = candidate;
  candidate->state = TASK_RUNNING;
  __asm__ volatile("csrw mscratch, %0" ::"r"(
      scheduler.harts_current[hartid]->kernel_stack_top));
  return_to_user(scheduler.harts_current[hartid]->sp);
}