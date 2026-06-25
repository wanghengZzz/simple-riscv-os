# Simple RISC-V OS

A bare-metal RISC-V 64-bit operating system running on QEMU's `virt` machine, featuring multi-core SMP boot, preemptive task scheduling, VirtIO block device I/O, and a complete trap/syscall mechanism.

The system runs exclusively in **Machine Mode (M-mode)** and **User Mode (U-mode)** — Supervisor Mode is not used. The kernel executes in M-mode and handles all traps directly; tasks run in U-mode and interact with the kernel via `ecall`.

---

## Features

### Multi-Core SMP Boot (`entry.S`, `smp.c`)
- Hart 0 handles primary initialization: copies `.text` / `.rodata` / `.data` to RAM, clears BSS, and initializes UART
- Other harts spin-wait on the `_init_finish` flag, then enter their respective `minor_main`
- Each hart has an independent 32KB kernel stack
- IPIs (Inter-Processor Interrupts) are sent via CLINT MSIP registers to wake other harts

### Privilege Modes
- **Machine Mode (M-mode)** — the kernel, interrupt handlers, and all privileged operations run here. `mtvec`, `mepc`, `mstatus`, `mie`, and `mscratch` are managed directly.
- **User Mode (U-mode)** — all tasks run in U-mode. `mstatus.MPP` is set to `00` (U-mode) before `mret`, so the CPU drops privilege when returning to a task. Supervisor Mode is not used.
- The boundary crossing is handled by `return_to_user` (M-mode → U-mode via `mret`) and `_trap_entry` (U-mode → M-mode on any trap or interrupt).

### Trap & Syscall Handling (`trap.c`, `entry.S`)
- Full register context save/restore (`_trap_entry` / `return_to_user`)
- Supported interrupts:
  - **Machine Timer Interrupt** — triggers scheduler context switch
  - **Machine External Interrupt** — handles VirtIO device IRQs via PLIC
  - **Machine Software Interrupt** — IPI, triggers context switch
- Supported exceptions / syscalls:
  - `ECALL_SLEEP_MS` — puts the current task to sleep for a given number of milliseconds
  - `ECALL_BLOCK_WAIT` — blocks until an async VirtIO I/O operation completes
  - `ECALL_MUTEX_LOCK` / `ECALL_MUTEX_UNLOCK` — kernel mutex acquire/release
  - `ECALL_EXIT` — terminates the current task

### Scheduler (`sched.c`)
- Each hart maintains its own circular linked-list ready queue
- Priority-based scheduling (priority 0–7)
- Task states: `TASK_READY`, `TASK_RUNNING`, `TASK_SLEEPING`, `TASK_DEAD`
- Deferred free to avoid freeing one's own stack while still running
- `timer_ctx_switch` — invoked on timer interrupt, rotates tasks periodically
- `shced_ecall_timer` — handles `task_sleep_ms` ecall, wakes task after precise delay

### VirtIO Block Device (`virtio.c`)
- Block device driver compliant with VirtIO MMIO spec v2
- Async I/O: `blk_read_async` / `blk_write_async` submit and return immediately
- Sync I/O: `blk_read_sync` blocks via `ECALL_BLOCK_WAIT` until the ISR signals completion
- Uses a 3-descriptor chain per request: header → data → status
- ISR processes the used ring, frees descriptors, and wakes waiting tasks

### Kernel Mutex (`mutex.h`, `uart.c`)
- Fast-path locking via AMO (`amoswap`)
- On contention, yields CPU through `ECALL_MUTEX_LOCK` and joins a waiter queue
- `ECALL_MUTEX_UNLOCK` hands the lock directly to the next waiter, eliminating race windows
- UART output is protected by `kmutex_t uart_mutex` for multi-core safety

### PLIC (`plic.h`)
- Configures VirtIO IRQ priorities (sources 1–8)
- Enables M-mode external interrupts per hart with threshold set to 0

### CLINT (`clint.h`, `clint.c`)
- Reads `mtime` and writes `mtimecmp` to trigger timer interrupts
- `clint_arm_timer_ms` — sets the next interrupt in milliseconds
- `task_sleep_ms` — inline ecall helper for tasks to sleep

### Memory Layout (`risc-v.lds`)
| Region | Address | Description |
|--------|---------|-------------|
| ROM (`.boot`) | `0x80000000` | 1MB, holds boot code |
| RAM (`.text` onward) | `0x80100000` | 127MB |
| Stack top | `0x88000000` | 32KB per hart, grows downward |
| Heap | End of BSS ~ stack bottom | Used by picolibc `malloc` |

---

## Directory Structure

```
.
├── entry.S              # Boot code, trap entry, return_to_user
├── main.c               # boot_main / minor_main, task creation and init flow
├── trap.c / trap.h      # Trap handler, syscall dispatch
├── sched.c / sched.h    # Scheduler, context switch, ecall handlers
├── smp.c / smp.h        # SMP boot, IPI delivery
├── virtio.c / virtio.h  # VirtIO block device driver
├── clint.c / clint.h    # CLINT timer operations
├── plic.h               # PLIC interrupt controller configuration
├── uart.c / uart.h      # NS16550A UART driver (mutex-protected)
├── mutex.h              # Kernel mutex (AMO + ecall)
├── spinlock.h           # LR/SC spinlock
├── common.h             # Core structs: trap_frame_t, task_t, scheduler_t
├── types.h              # Basic types, MMIO access macros
├── utils.c / utils.h    # Safe memory allocation wrappers
├── risc-v.lds           # Linker script
└── Makefile             # Build and run rules
```   

---

## Requirements

- `riscv64-unknown-elf-gcc` (with picolibc)
- `qemu-system-riscv64` (>= 7.x recommended)
- GNU Make

Verify your toolchain is available:
```bash
riscv64-unknown-elf-gcc --version
qemu-system-riscv64 --version
```

---

## Build & Run

### Create a Disk Image

The VirtIO block device requires a raw disk image (`disk.img`). Create one before the first run:

```bash
dd if=/dev/zero of=disk.img bs=512 count=2048      # 1MB blank disk
# or with random data:
dd if=/dev/urandom of=disk.img bs=512 count=2048
```

### Build

```bash
make
```

### Run (QEMU)

```bash
make run
```

Launches QEMU with 4 cores, 128MB RAM, no BIOS, and `disk.img` attached. Output goes directly to the terminal.

Press `Ctrl+A` then `X` to exit QEMU.

### Debug Mode (GDB)

```bash
make DEBUG=1       # Compiles with -Og -ggdb3 and adds -s -S to QEMU
make run DEBUG=1   # QEMU starts paused, waiting for a GDB connection
```

In a second terminal:

```bash
gdb-multiarch
(gdb) file build/RISCVDemo.axf
(gdb) target remote :1234
(gdb) c
```

### Clean

```bash
make clean
```

### Format Code

```bash
make format    # Runs clang-format (Google style) on all .c / .h files
```

### Inspect QEMU Device Tree

```bash
bash check_qemu.sh <output_prefix>
# Produces <output_prefix>.dtb and <output_prefix>.dts
```

---

## Sample Output

```
[hart 0] [task A] running
[hart 0] [task B] running
[hart 1] [task A] running
[hart 0] [task C] running
fib: 9227465
hartid: 0
<disk content>
...
```

---

## Key Configuration Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `MAX_HARTS` | 4 | Maximum number of harts |
| `MAX_TASK` | 128 | Maximum number of tasks |
| `MAX_STACK_SIZE` | 2048 × 8 bytes | User/kernel stack size per task |
| `TIMEBASE_FREQ` | 10,000,000 Hz | CLINT clock frequency |
| `TIMER_INT_MS` | 100 ms | Timer interrupt interval (reference) |
| `VIRTQ_SIZE` | 16 | VirtIO virtqueue size |
| `NUM_SECTORS` | 10 | Sectors read per I/O operation |
