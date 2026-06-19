/* virtio_blk.c — VirtIO Block Device Driver
 *
 * Data Flow (using Read as an example):
 *
 * blk_read(sector, buf, n)
 * ↓ Populate 3 descriptors + push to avail ring + kick (QUEUE_NOTIFY)
 * ↓ Returns immediately, CPU switches to run other tasks
 *
 * QEMU asynchronously transfers data to buf (DMA semantics)
 * ↓ Upon completion, updates used ring + sends PLIC interrupt
 *
 * virtio_blk_isr() (external interrupt handler)
 * ↓ Scans used ring to find completed requests
 * ↓ Validates status byte (OK/ERROR)
 * ↓ sem_post/unblocks the waiting task
 *
 * Task wakes up; data is now available in buf
 */

#include "virtio.h"

#include "clint.h"
#include "spinlock.h"
#include "uart.h"

/* ---- VirtIO Block Request Types (VirtIO spec 5.2.6) ---- */
#define VIRTIO_BLK_T_IN 0    /* sector → buf (read) */
#define VIRTIO_BLK_T_OUT 1   /* buf → sector (write) */
#define VIRTIO_BLK_T_FLUSH 4 /* flush write cache */

/* ---- VirtIO Block Request Status ---- */
#define VIRTIO_BLK_S_OK 0     /* Success */
#define VIRTIO_BLK_S_IOERR 1  /* I/O Error */
#define VIRTIO_BLK_S_UNSUPP 2 /* Unsupported operation */

/* A request chain consists of three parts:
 * desc[0]: header (written by driver, READ-ONLY for device)
 * desc[1]: data   (read=WRITE for device, write=READ-ONLY for device)
 * desc[2]: status (not written by driver, WRITE for device)
 *
 * Note: desc[0] addr must point to blk_req_hdr, and data must be 512-byte aligned */

typedef struct {
  uint32_t type; /* VIRTIO_BLK_T_* */
  uint32_t reserved;
  uint64_t sector; /* Starting sector (512 bytes/sector) */
} __attribute__((packed)) blk_req_hdr_t;

/* ---- Device State ---- */
#define BLK_QUEUE_IDX 0 /* Block devices use only one virtqueue */

typedef struct {
  int slot;          /* MMIO slot number */
  virtqueue_t vq;    /* Virtqueue (desc + avail + used) */
  uint64_t capacity; /* Total capacity of the device in sectors */
  spinlock_t lock;
  /* Track inflight requests per descriptor chain to recover context in ISR */
  struct {
    blk_req_hdr_t hdr; /* Request header for each slot */
    uint8_t status;    /* Device writes the status byte here */
    /* In a full RTOS, this would be a semaphore: semaphore_t sem; */
    volatile int done; /* ISR sets this to true; task uses it to poll/check */
    task_t* waiting_task;
  } inflight[VIRTQ_SIZE];

} blk_dev_t;

static blk_dev_t blk;

static uint16_t vq_alloc_desc(virtqueue_t* vq) {
  if (vq->num_free == 0) return VIRTQ_SIZE;
  uint16_t idx = vq->free_head;
  vq->free_head = vq->desc[idx].next;
  vq->num_free--;
  return idx;
}

static void vq_free_desc(virtqueue_t* vq, uint16_t idx) {
  vq->desc[idx].next = vq->free_head;
  vq->free_head = idx;
  vq->num_free++;
}

static void vq_init_free_list(virtqueue_t* vq) {
  for (uint16_t i = 0; i < VIRTQ_SIZE - 1; ++i) vq->desc[i].next = i + 1;
  vq->desc[VIRTQ_SIZE - 1].next = VIRTQ_SIZE;
  vq->free_head = 0;
  vq->num_free = VIRTQ_SIZE;
}

static void vq_kick(int slot, uint16_t queue_idx) {
  __asm__ volatile("fence w, w" ::: "memory");
  virtio_write(slot, VREG_QUEUE_NOTIFY, queue_idx);
}

int virtio_blk_init(int slot) {
  /* 0. Verify this is a valid VirtIO block device */
  if (virtio_read(slot, VREG_MAGIC) != VIRTIO_MAGIC ||
      virtio_read(slot, VREG_VERSION) != VIRTIO_VERSION ||
      virtio_read(slot, VREG_DEVICE_ID) != VIRTIO_DEV_BLK) {
    return -1;
  }

  blk.slot = slot;

  /* 1. Reset device */
  virtio_write(slot, VREG_STATUS, 0);
  /* 2. ACKNOWLEDGE: Device detected */
  virtio_write(slot, VREG_STATUS, VIRTIO_S_ACKNOWLEDGE);
  /* 3. DRIVER: Driver knows how to manage the device */
  virtio_write(slot, VREG_STATUS, VIRTIO_S_ACKNOWLEDGE | VIRTIO_S_DRIVER);
  /* 4. Feature Negotiation
   * Accept only features supported by the device, no extra demands.
   * A production driver should check and configure required feature bits 
   * (e.g., BLK_F_SEG_MAX, etc.) */
  virtio_write(slot, VREG_DEV_FEAT_SEL, 0);
  uint32_t features = virtio_read(slot, VREG_DEV_FEATURES);
  virtio_write(slot, VREG_DRV_FEAT_SEL, 0);
  virtio_write(slot, VREG_DRV_FEATURES,
               features & 0); /* Do not demand any optional features */

  /* 5. FEATURES_OK: Feature negotiation acknowledged */

  virtio_write(slot, VREG_STATUS,
               VIRTIO_S_ACKNOWLEDGE | VIRTIO_S_DRIVER | VIRTIO_S_FEATURES_OK);

  /* Verify the device accepted the features (continue only if bit remains set) */
  uint32_t st = virtio_read(slot, VREG_STATUS);
  if (!(st & VIRTIO_S_FEATURES_OK)) {
    virtio_write(slot, VREG_STATUS, VIRTIO_S_FAILED);
    return -2;
  }

  /* 6. Setup virtqueue 0 (block devices only use one queue) */
  virtio_write(slot, VREG_QUEUE_SEL, BLK_QUEUE_IDX);

  uint32_t qmax = virtio_read(slot, VREG_QUEUE_NUM_MAX);
  if (qmax < VIRTQ_SIZE) {
    virtio_write(slot, VREG_STATUS, VIRTIO_S_FAILED);
    return -3;
  }
  virtio_write(slot, VREG_QUEUE_NUM, VIRTQ_SIZE);

  /* Provide the memory addresses of the three shared rings to the device */
  virtio_set_addr(slot, VREG_QUEUE_DESC_LO, blk.vq.desc);
  virtio_set_addr(slot, VREG_QUEUE_AVAIL_LO, &blk.vq.avail);
  virtio_set_addr(slot, VREG_QUEUE_USED_LO, &blk.vq.used);

  virtio_write(slot, VREG_QUEUE_READY, 1);

  /* Initialize descriptor free list */
  vq_init_free_list(&blk.vq);
  blk.vq.last_used_idx = 0;

  /* Read device capacity (first 8 bytes of block config space = capacity in sectors) */
  blk.capacity = *(volatile uint64_t*)(VIRTIO_BASE(slot) + VREG_CONFIG);

  /* 7. DRIVER_OK: Initialization complete, device live and active */
  virtio_write(slot, VREG_STATUS,
               VIRTIO_S_ACKNOWLEDGE | VIRTIO_S_DRIVER | VIRTIO_S_FEATURES_OK |
                   VIRTIO_S_DRIVER_OK);

  // uart_printf("[virtio_blk] slot %d ready, capacity=%u sectors\n",
  //             slot, (uint32_t)blk.capacity);
  blk.lock = SPINLOCK_INIT;
  return 0;
}

/* ================================================================
 * Asynchronous Read / Write
 *
 * Each request allocates 3 descriptors structured as follows:
 *
 * +----------+      +----------+      +----------+
 * | desc[d0] | NEXT | desc[d1] | NEXT | desc[d2] |
 * | header   | ---> | data buf | ---> | status   |
 * | (READ)   |      | (WRITE)  |      | (WRITE)  |
 * +----------+      +----------+      +----------+
 *
 * avail.ring[avail.idx % N] = d0    ← Instructs device to start from d0
 * avail.idx++
 * kick
 * ================================================================ */

static int blk_submit(uint32_t type, uint64_t sector, void* buf,
                      uint32_t buf_len) {
  virtqueue_t* vq = &blk.vq;
  spin_lock(&blk.lock);
  if (vq->num_free < 3) {
    spin_unlock(&blk.lock);
    return -1;
  }

  /* Allocate 3 descriptors */
  uint16_t d0 = vq_alloc_desc(vq);
  uint16_t d1 = vq_alloc_desc(vq);
  uint16_t d2 = vq_alloc_desc(vq);

  /* Use the d0 index as the tracking key for the inflight slot */
  blk.inflight[d0].hdr.type = type;
  blk.inflight[d0].hdr.reserved = 0;
  blk.inflight[d0].hdr.sector = sector;
  blk.inflight[d0].status = 0xff;
  blk.inflight[d0].done = 0;
  blk.inflight[d0].waiting_task = nullptr;

  /* desc[d0]: Header, device-readable */
  vq->desc[d0].addr = (uintptr_t)&blk.inflight[d0].hdr;
  vq->desc[d0].len = sizeof(blk_req_hdr_t);
  vq->desc[d0].flags = VIRTQ_DESC_F_NEXT;
  vq->desc[d0].next = d1;

  /* desc[d1]: Data buffer
   * READ (T_IN)   = Device writes to buf → VIRTQ_DESC_F_WRITE
   * WRITE (T_OUT) = Device reads from buf → Clear WRITE flag */
  vq->desc[d1].addr = (uintptr_t)buf;
  vq->desc[d1].len = buf_len;
  vq->desc[d1].flags =
      VIRTQ_DESC_F_NEXT | (type == VIRTIO_BLK_T_IN ? VIRTQ_DESC_F_WRITE : 0);
  vq->desc[d1].next = d2;

  /* desc[d2]: Status byte, device writes result code here */
  vq->desc[d2].addr = (uintptr_t)&blk.inflight[d0].status;
  vq->desc[d2].len = 1;
  vq->desc[d2].flags = VIRTQ_DESC_F_WRITE;
  vq->desc[d2].next = 0;

  /* Push d0 to available ring
   * fence w,w: Ensures descriptor stores are visible to the device before updating avail ring */
  __asm__ volatile("fence w, w" ::: "memory");

  uint16_t avail_idx = vq->avail.idx % VIRTQ_SIZE;
  vq->avail.ring[avail_idx] = d0;

  __asm__ volatile("fence w, w" ::: "memory");

  vq->avail.idx++;

  /* Kick the device to trigger execution */
  vq_kick(blk.slot, BLK_QUEUE_IDX);
  spin_unlock(&blk.lock);
  return d0;
}

/* ================================================================
 * ISR: Called when an external interrupt fires
 * Parses the used ring and updates all completed requests
 * ================================================================ */

int virtio_blk_isr(void) {
  spin_lock(&blk.lock);
  uint32_t irq_status = virtio_read(blk.slot, VREG_IRQ_STATUS);
  if (!(irq_status & VIRTIO_IRQ_USED_RING)) {
    spin_unlock(&blk.lock);
    return 0;
  }

  /* Acknowledge interrupt */
  virtio_write(blk.slot, VREG_IRQ_ACK, irq_status);

  virtqueue_t* vq = &blk.vq;
  int woke_task = 0;

  /* fence r,r: Ensures device updates to the used ring are visible */
  __asm__ volatile("fence r, r" ::: "memory");

  /* Process all newly completed entries in the used ring */
  while (vq->last_used_idx != vq->used.idx) {
    uint16_t idx = vq->last_used_idx % VIRTQ_SIZE;
    uint16_t d0 = (uint16_t)vq->used.ring[idx].id;

    /* Validate completion status */
    // uint8_t status = blk.inflight[d0].status;
    // if (status != VIRTIO_BLK_S_OK) {
    //     uart_printf("[virtio_blk] request d0=%u failed, status=%u\n",
    //                 d0, status);
    // }

    /* Mark completion and unblock waiting task */
    /* Production RTOS equivalent: sem_post_from_isr(&blk.inflight[d0].sem); */
    blk.inflight[d0].done = 1;

    if (blk.inflight[d0].waiting_task) {
      blk.inflight[d0].waiting_task->state = TASK_READY;
      blk.inflight[d0].waiting_task->block_token = -1;
      blk.inflight[d0].waiting_task = nullptr;
    }

    woke_task = 1;

    /* Reclaim the 3 descriptors (traverse the next-pointer chain from d0) */
    uint16_t d1 = vq->desc[d0].next;
    uint16_t d2 = vq->desc[d1].next;
    vq_free_desc(vq, d2);
    vq_free_desc(vq, d1);
    vq_free_desc(vq, d0);

    vq->last_used_idx++;
  }
  spin_unlock(&blk.lock);
  return woke_task; /* Return true to indicate a context switch is required */
}

/* ================================================================
 * Public APIs (Task-space Interface)
 * ================================================================ */

/* Asynchronous Read: returns immediately after submission; data updates via ISR later */
int blk_read_async(uint64_t sector, void* buf, uint32_t sectors) {
  return blk_submit(VIRTIO_BLK_T_IN, sector, buf, sectors * 512);
}

/* Asynchronous Write */
int blk_write_async(uint64_t sector, const void* buf, uint32_t sectors) {
  return blk_submit(VIRTIO_BLK_T_OUT, sector, (void*)buf, sectors * 512);
}

/* Synchronous Read (Basic implementation: busy-waits done flag; replace with sem_wait in production) */
int blk_read_sync(uint64_t sector, void* buf, uint32_t sectors) {
  int token = blk_read_async(sector, buf, sectors);
  if (token < 0) return token;

  register uintptr_t a0 __asm__("a0") = (uintptr_t)token;
  register uintptr_t a7 __asm__("a7") = ECALL_BLOCK_WAIT;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");

  return blk.inflight[token].status == VIRTIO_BLK_S_OK ? 0 : -1;
}

void virtio_blk_wake_blocked_tasks(void) { virtio_blk_isr(); }

void virtio_blk_set_waiter(int token, task_t* t) {
  if (token >= 0 && token < VIRTQ_SIZE) blk.inflight[token].waiting_task = t;
}

int virtio_blk_is_done(int token) {
  if (token < 0 || token >= VIRTQ_SIZE) return 1;
  return blk.inflight[token].done;
}