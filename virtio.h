#ifndef VIRTIO_H_
#define VIRTIO_H_

#include "common.h"
#include "types.h"

/* ================================================================
 * VirtIO MMIO Device Layout (QEMU virt machine)
 *
 * 8 device slots, 4KB each:
 * slot 0: 0x10001000  (PLIC source 1)
 * slot 1: 0x10002000  (PLIC source 2)
 * ...
 * slot 7: 0x10008000  (PLIC source 8)
 * ================================================================ */
#define VIRTIO_MMIO_BASE 0x10001000UL
#define VIRTIO_MMIO_STRIDE 0x1000UL
#define VIRTIO_MMIO_COUNT 8

/* ---- MMIO Register Offsets ---- */
#define VREG_MAGIC          0x000 /* R:  Must be 0x74726976 ("virt") */
#define VREG_VERSION        0x004 /* R:  Must be 2 (modern) */
#define VREG_DEVICE_ID      0x008 /* R:  Device subsystem type */
#define VREG_VENDOR_ID      0x00C /* R:  Vendor ID */
#define VREG_DEV_FEATURES   0x010 /* R:  Device feature bits flags */
#define VREG_DEV_FEAT_SEL   0x014 /* W:  Select device feature page (0/1) */
#define VREG_DRV_FEATURES   0x020 /* W:  Features accepted by driver */
#define VREG_DRV_FEAT_SEL   0x024 /* W:  Select driver feature page (0/1) */
#define VREG_QUEUE_SEL      0x030 /* W:  Select virtqueue index */
#define VREG_QUEUE_NUM_MAX  0x034 /* R:  Maximum queue size supported by device */
#define VREG_QUEUE_NUM      0x038 /* W:  Set active queue size */
#define VREG_QUEUE_READY    0x044 /* RW: 1 = Queue is enabled/ready */
#define VREG_QUEUE_NOTIFY   0x050 /* W:  Write queue index to notify/kick device */
#define VREG_IRQ_STATUS     0x060 /* R:  Interrupt status bit mask */
#define VREG_IRQ_ACK        0x064 /* W:  Acknowledge/clear interrupt flags */
#define VREG_STATUS         0x070 /* RW: Device status state machine */
#define VREG_QUEUE_DESC_LO  0x080 /* W:  Descriptor table physical address low 32-bit */
#define VREG_QUEUE_DESC_HI  0x084 /* W:  Descriptor table physical address high 32-bit */
#define VREG_QUEUE_AVAIL_LO 0x090 /* W:  Available ring physical address low 32-bit */
#define VREG_QUEUE_AVAIL_HI 0x094 /* W:  Available ring physical address high 32-bit */
#define VREG_QUEUE_USED_LO  0x0A0 /* W:  Used ring physical address low 32-bit */
#define VREG_QUEUE_USED_HI  0x0A4 /* W:  Used ring physical address high 32-bit */
#define VREG_CONFIG         0x100 /* R:  Device-specific configuration space */

/* ---- Magic & Device IDs ---- */
#define VIRTIO_MAGIC 0x74726976UL /* "virt" in little-endian */
#define VIRTIO_VERSION 2

#define VIRTIO_DEV_NET     1 /* Network Card */
#define VIRTIO_DEV_BLK     2 /* Block Device */
#define VIRTIO_DEV_CONSOLE 3 /* Virtual Console */
#define VIRTIO_DEV_RNG     4 /* Entropy Source / Random Number Generator */

/* ---- Device Status Machine Bits (Set sequentially) ---- */
#define VIRTIO_S_ACKNOWLEDGE (1 << 0) /* 1. OS has discovered the device */
#define VIRTIO_S_DRIVER      (1 << 1) /* 2. OS knows how to drive the device */
#define VIRTIO_S_DRIVER_OK   (1 << 2) /* 4. Driver setup completed successfully */
#define VIRTIO_S_FEATURES_OK (1 << 3) /* 3. Feature negotiation acknowledged */
#define VIRTIO_S_NEEDS_RESET (1 << 6) /* Device encountered an unrecoverable error */
#define VIRTIO_S_FAILED      (1 << 7) /* Driver gave up on the device */

/* ---- Interrupt Status Bits ---- */
#define VIRTIO_IRQ_USED_RING (1 << 0) /* Device updated the used ring */
#define VIRTIO_IRQ_CONFIG    (1 << 1) /* Configuration space changed */

#define VIRTQ_SIZE 16 /* Must be a power of 2, less than or equal to QUEUE_NUM_MAX */

/* Descriptor flags */
#define VIRTQ_DESC_F_NEXT     (1 << 0) /* Marks a chain; another descriptor follows */
#define VIRTQ_DESC_F_WRITE    (1 << 1) /* Buffer is device-writeable (else device-readable) */
#define VIRTQ_DESC_F_INDIRECT (1 << 2) /* Buffer contains a table of indirect descriptors */

/* Available ring flags */
#define VIRTQ_AVAIL_F_NO_IRQ (1 << 0) /* Suppress device interrupts (used for polling mode) */

/* Used ring flags */
#define VIRTQ_USED_F_NO_NOTIFY (1 << 0) /* Suppress driver notifications / kicks */

/* Descriptor Table Entry (16 bytes, must be 16-byte aligned) */
typedef struct {
  uint64_t addr;  /* Physical address of the buffer */
  uint32_t len;   /* Length of the buffer in bytes */
  uint16_t flags; /* VIRTQ_DESC_F_* flags */
  uint16_t next;  /* Index of the next descriptor (valid if NEXT flag is set) */
} __attribute__((packed)) virtq_desc_t;

/* Available Ring (Driver → Device, 2-byte aligned) */
typedef struct {
  uint16_t flags;
  uint16_t idx;              /* Index where the driver will place the next entry */
  uint16_t ring[VIRTQ_SIZE]; /* Head indices of the descriptor chains */
  uint16_t used_event;       /* Interrupt suppression threshold (optional feature) */
} __attribute__((packed)) virtq_avail_t;

/* Used Ring Element */
typedef struct {
  uint32_t id;  /* Index of the start of the completed descriptor chain */
  uint32_t len; /* Total number of bytes written by the device */
} __attribute__((packed)) virtq_used_elem_t;

/* Used Ring (Device → Driver, 4-byte aligned) */
typedef struct {
  uint16_t flags;
  uint16_t idx; /* Index where the device will place the next entry */
  virtq_used_elem_t ring[VIRTQ_SIZE];
  uint16_t avail_event;
} __attribute__((packed)) virtq_used_t;

/* Complete Virtqueue structure (Software tracking state + 3 shared memory regions) */
typedef struct {
  /* Shared Memory Regions (Device-visible; must be uncacheable or synchronized with fences) */
  virtq_desc_t desc[VIRTQ_SIZE] __attribute__((aligned(16)));
  virtq_avail_t avail __attribute__((aligned(2)));
  uint8_t _pad[2];
  virtq_used_t used __attribute__((aligned(4)));

  /* Driver Private State (Hidden from the device) */
  uint16_t last_used_idx; /* Last snapshot of used.idx processed by driver */
  uint16_t free_head;     /* Head of the free descriptor linked list */
  uint16_t num_free;      /* Total number of currently available descriptors */
  uint16_t _rsvd;
} virtqueue_t;

#define VIRTIO_BASE(slot) (1UL * VIRTIO_MMIO_BASE + (slot) * VIRTIO_MMIO_STRIDE)

static inline uint32_t virtio_read(int slot, uint32_t reg) {
  return REG32(VIRTIO_BASE(slot) + (reg));
}

static inline void virtio_write(int slot, uint32_t reg, uint32_t val) {
  REG32(VIRTIO_BASE(slot) + (reg)) = val;
}

static inline void virtio_set_addr(int slot, uint32_t reg_lo, void* ptr) {
  uintptr_t pa = (uintptr_t)ptr;
  virtio_write(slot, reg_lo, (uint32_t)(pa & 0xffffffff));
  virtio_write(slot, reg_lo + 4, (uint32_t)(pa >> 32));
}

static inline int virtio_blk_slot() {
  for (int i = 0; i < VIRTIO_MMIO_COUNT; i++) {
    if (virtio_read(i, VREG_MAGIC) == VIRTIO_MAGIC &&
        virtio_read(i, VREG_VERSION) == VIRTIO_VERSION &&
        virtio_read(i, VREG_DEVICE_ID) == VIRTIO_DEV_BLK) {
      return i;
    }
  }
  return -1;
}

int virtio_blk_init(int slot);
int blk_read_sync(uint64_t sector, void* buf, uint32_t sectors);
int blk_write_async(uint64_t sector, const void* buf, uint32_t sectors);
int blk_read_async(uint64_t sector, void* buf, uint32_t sectors);
int virtio_blk_isr(void);

void virtio_blk_wake_blocked_tasks(void);
void virtio_blk_set_waiter(int token, task_t* t);
int virtio_blk_is_done(int token);

#endif