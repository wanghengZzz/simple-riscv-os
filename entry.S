#include "riscv-reg.h"

#define SYSCON_BASE 0x100000
#define SYSCON_POWEROFF 0x5555

#define STACK_SIZE 0x8000

.section .text.boot, "ax", @progbits
.global _start
.align 2

_start:
    csrr t0, mhartid

    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

    mv s0, a0
    mv s1, a1
    mv s2, a2
    mv s3, a3
    csrw mie, zero
    csrw mip, zero
    csrc mstatus, (1 << 3)
    csrw mideleg, zero
    csrw medeleg, zero

    la      sp, _stack_top
    li      t1, STACK_SIZE
    mul     t1, t1, a0
    sub     sp, sp, t1
    andi    sp, sp, -16
    bnez    t0, .Lwait_hart0
    j .Lcopy_text

.Lcopy_task:
    bgeu a0, a1, .Lcopy_over
    LOAD a3, 0(a2)
    STOR a3, 0(a0)
    addi a2, a2, REGSIZE
    addi a0, a0, REGSIZE
    j .Lcopy_task
.Lcopy_over:
    ret

.Lcopy_text:
    la a0, _text_start
    la a1, _text_end
    la a2, _text_lma
    addi sp, sp, -16
    STOR ra, 0(sp)
    call .Lcopy_task
    LOAD ra, 0(sp)
    addi sp, sp, 16

.Lcopy_rodata:
    la a0, _rodata_start
    la a1, _rodata_end
    la a2, _rodata_lma
    addi sp, sp, -16
    STOR ra, 0(sp)
    call .Lcopy_task
    LOAD ra, 0(sp)
    addi sp, sp, 16

.Lcopy_data:
    la a0, _data_start
    la a1, _data_end
    la a2, _data_lma
    addi sp, sp, -16
    STOR ra, 0(sp)
    call .Lcopy_task
    LOAD ra, 0(sp)
    addi sp, sp, 16

.Lrom_done:

    la t0, _bss_start
    la t1, _bss_end

.Lbss_clear:
    bgeu t0, t1, .Lbss_done
    STOR zero, 0(t0)
    addi t0, t0, REGSIZE
    j .Lbss_clear

.Lwait_hart0:
    la t0, _init_finish
1:
    LOAD t1, 0(t0)
    beqz t1, 1b
    j .Linit_trap

.Lbss_done:
    la t0, _init_finish
    li t1, 1
    STOR t1, 0(t0)

.Linit_trap:
    la t0, _trap_entry
    csrw mtvec, t0
    csrw mscratch, sp
    li  t0, -1
    csrw pmpaddr0, t0
    li  t0, 0x1f
    csrw pmpcfg0, t0

    mv a0, s0
    mv a1, s1
    mv a2, s2
    mv a3, s3
    beqz a0, .Lmain_hart0
    la t0, _main_finish
1:
    LOAD t1, 0(t0)
    beqz t1, 1b
    call minor_main
    ret
.Lmain_hart0:
    call boot_main
    ret

.Lhalt:
    wfi
    j .Lhalt

.Lhart_park:
    wfi
    j .Lhart_park


.section .text
.align 2

_trap_entry:

    csrrw   sp, mscratch, sp
    addi    sp, sp, -(REGSIZE * 32)

    STOR    t0,  (REGSIZE *  4)(sp)
    csrr    t0, mscratch
    STOR    t0,  (REGSIZE *  1)(sp)

    STOR    ra,  (REGSIZE *  0)(sp)
    STOR    gp,  (REGSIZE *  2)(sp)
    STOR    tp,  (REGSIZE *  3)(sp)

    STOR    t1,  (REGSIZE *  5)(sp)
    STOR    t2,  (REGSIZE *  6)(sp)
    STOR    s0,  (REGSIZE *  7)(sp)
    STOR    s1,  (REGSIZE *  8)(sp)
    STOR    a0,  (REGSIZE *  9)(sp)
    STOR    a1,  (REGSIZE * 10)(sp)
    STOR    a2,  (REGSIZE * 11)(sp)
    STOR    a3,  (REGSIZE * 12)(sp)
    STOR    a4,  (REGSIZE * 13)(sp)
    STOR    a5,  (REGSIZE * 14)(sp)
    STOR    a6,  (REGSIZE * 15)(sp)
    STOR    a7,  (REGSIZE * 16)(sp)
    STOR    s2,  (REGSIZE * 17)(sp)
    STOR    s3,  (REGSIZE * 18)(sp)
    STOR    s4,  (REGSIZE * 19)(sp)
    STOR    s5,  (REGSIZE * 20)(sp)
    STOR    s6,  (REGSIZE * 21)(sp)
    STOR    s7,  (REGSIZE * 22)(sp)
    STOR    s8,  (REGSIZE * 23)(sp)
    STOR    s9,  (REGSIZE * 24)(sp)
    STOR    s10, (REGSIZE * 25)(sp)
    STOR    s11, (REGSIZE * 26)(sp)
    STOR    t3,  (REGSIZE * 27)(sp)
    STOR    t4,  (REGSIZE * 28)(sp)
    STOR    t5,  (REGSIZE * 29)(sp)
    STOR    t6,  (REGSIZE * 30)(sp)

    csrr    t0,  mepc
    STOR    t0,  (REGSIZE * 31)(sp)

    csrr    a0, mcause
    csrr    a1, mepc
    csrr    a2, mtval
    mv      a3, sp
    call    trap_handler

.Ltrap_halt:
    wfi
    j   .Ltrap_halt

.global return_to_user
.align 2
return_to_user:

    li      t0, (3 << 11)
    csrc    mstatus, t0
    li      t0, (1 << 7)
    csrs    mstatus, t0

    LOAD    t0,  (REGSIZE * 31)(a0)
    csrw    mepc, t0

    mv      sp, a0

    LOAD    ra,  (REGSIZE *  0)(sp)
    LOAD    gp,  (REGSIZE *  2)(sp)
    LOAD    tp,  (REGSIZE *  3)(sp)
    LOAD    t1,  (REGSIZE *  5)(sp)
    LOAD    t2,  (REGSIZE *  6)(sp)
    LOAD    s0,  (REGSIZE *  7)(sp)
    LOAD    s1,  (REGSIZE *  8)(sp)
    LOAD    a0,  (REGSIZE *  9)(sp)
    LOAD    a1,  (REGSIZE * 10)(sp)
    LOAD    a2,  (REGSIZE * 11)(sp)
    LOAD    a3,  (REGSIZE * 12)(sp)
    LOAD    a4,  (REGSIZE * 13)(sp)
    LOAD    a5,  (REGSIZE * 14)(sp)
    LOAD    a6,  (REGSIZE * 15)(sp)
    LOAD    a7,  (REGSIZE * 16)(sp)
    LOAD    s2,  (REGSIZE * 17)(sp)
    LOAD    s3,  (REGSIZE * 18)(sp)
    LOAD    s4,  (REGSIZE * 19)(sp)
    LOAD    s5,  (REGSIZE * 20)(sp)
    LOAD    s6,  (REGSIZE * 21)(sp)
    LOAD    s7,  (REGSIZE * 22)(sp)
    LOAD    s8,  (REGSIZE * 23)(sp)
    LOAD    s9,  (REGSIZE * 24)(sp)
    LOAD    s10, (REGSIZE * 25)(sp)
    LOAD    s11, (REGSIZE * 26)(sp)
    LOAD    t3,  (REGSIZE * 27)(sp)
    LOAD    t4,  (REGSIZE * 28)(sp)
    LOAD    t5,  (REGSIZE * 29)(sp)
    LOAD    t6,  (REGSIZE * 30)(sp)

    LOAD    t0,  (REGSIZE * 4)(sp)
    LOAD    sp,  (REGSIZE * 1)(sp)

    mret

