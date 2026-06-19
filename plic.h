#ifndef PLIC_H_
#define PLIC_H_
#include "smp.h"
#define PLIC_BASE 0x0C000000UL
#define PLIC_PRIORITY(id) REG32(PLIC_BASE + ((id) << 2))
#define PLIC_PENDING(id) REG32(PLIC_BASE + 0x1000 + ((id) >> 5) * 4)
#define PLIC_M_ENABLE(hart) REG32(PLIC_BASE + 0x2000 + (2 * (hart)) * 0x80)
#define PLIC_M_THRESHOLD(hart) \
  REG32(PLIC_BASE + 0x200000 + (2 * (hart)) * 0x1000)
#define PLIC_M_CLAIM(hart) REG32(PLIC_BASE + 0x200004 + (2 * (hart)) * 0x1000)
#define PLIC_M_COMPLETE(hart) \
  REG32(PLIC_BASE + 0x200004 + (2 * (hart)) * 0x1000)

enum {
  UART0_IRQ = 10,
  RTC_IRQ = 11,
  VIRTIO_IRQ = 1, /* 1 to 8 */
  VIRTIO_COUNT = 8,
  PCIE_IRQ = 0x20,            /* 32 to 35 */
  IOMMU_SYS_IRQ = 0x24,       /* 36-39 */
  VIRT_PLATFORM_BUS_IRQ = 64, /* 64 to 95 */
};

static inline void plic_init(uintptr_t hartid) {
  if (hartid == 0) {
    for (int i = VIRTIO_IRQ; i < VIRTIO_IRQ + VIRTIO_COUNT; ++i)
      PLIC_PRIORITY(i) = 1;
  }

  // enable IRQ 1~8，bit mask
  PLIC_M_ENABLE(hartid) = 0x1FE;

  PLIC_M_THRESHOLD(hartid) = 0;
}

#endif
