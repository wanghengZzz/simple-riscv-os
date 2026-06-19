#ifndef TYPES_H_
#define TYPES_H_

#include <stddef.h>
#include <stdint.h>
#include "utils.h"
#define UL_SIZE sizeof(uintptr_t)
#define UL_NUM_BITS (UL_SIZE << 3)
#define REG8(addr) (*(volatile uint8_t *)(addr))
#define REG32(addr) (*(volatile uint32_t *)(addr))
#define REG64(addr) (*(volatile uint64_t *)(addr))

#if __riscv_xlen == 32
#define MEM_ACCESS(addr) REG32(addr)
#else
#define MEM_ACCESS(addr) REG64(addr)
#endif

#define REG_ACCESS MEM_ACCESS

typedef uint64_t riscv_time_t;
typedef uintptr_t lock_size_t;

#endif