
#include "clint.h"

void clint_delay_ms(riscv_time_t ms) {
  riscv_time_t target = clint_get_time() + (TIMEBASE_FREQ / 1000) * ms;
  while (clint_get_time() < target);
}