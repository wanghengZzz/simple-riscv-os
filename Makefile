
CROSS = riscv64-unknown-elf-
CC = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
ARCH = $(CROSS)ar

# Generate GCC_VERSION in number format
GCC_VERSION = $(shell $(CC) --version | grep ^$(CC) | sed 's/^.* //g' | awk -F. '{ printf("%d%02d%02d"), $$1, $$2, $$3 }')
GCC_VERSION_NEED_ZICSR = "110100"
DEBUG = 0

BUILD_DIR = build
BIN = $(BUILD_DIR)/RISCVDemo.axf

QEMU = qemu-system-riscv64
QEMU_OPTS = \
    -machine virt \
    -cpu rv64 \
    -smp 4 \
    -m 128M \
    -nographic \
    -bios none \
    -kernel $(BIN) \
    -drive file=disk.img,if=none,format=raw,id=hd0 \
    -device virtio-blk-device,drive=hd0 \
    -global virtio-mmio.force-legacy=false

ifeq ($(DEBUG), 1)
    QEMU_OPTS += -s -S
endif

CFLAGS = \
    -mabi=lp64 -mcmodel=medany \
    -Wall \
    -fmessage-length=0 \
    -ffunction-sections \
    -fdata-sections \
    -fno-builtin-printf

ifeq ($(shell test $(GCC_VERSION) -ge $(GCC_VERSION_NEED_ZICSR) && echo true),true)
    CFLAGS += -march=rv64imac_zicsr
else
    CFLAGS += -march=rv64imac
endif

LDFLAGS = -Trisc-v.lds \
    -march=rv64imac -mabi=lp64 -mcmodel=medany \
    -nostartfiles \
    -Xlinker --gc-sections \
    -Xlinker --defsym=__stack_size=0x8000 \
    -Xlinker -Map=$(BUILD_DIR)/RISCVDemo.map

ifeq ($(DEBUG), 1)
    CFLAGS += -Og -ggdb3
else
    CFLAGS += -O2
endif

PICOLIBC = 1
ifeq ($(PICOLIBC), 1)
    CFLAGS += --specs=picolibc.specs -DPICOLIBC_INTEGER_PRINTF_SCANF
    LDFLAGS += --specs=picolibc.specs -DPICOLIBC_INTEGER_PRINTF_SCANF
endif

SRCS = main.c trap.c \
    clint.c uart.c virtio.c \
    sched.c utils.c smp.c \

ASMS = entry.S

OBJS = $(SRCS:%.c=$(BUILD_DIR)/%.o) $(ASMS:%.S=$(BUILD_DIR)/%.o)
DEPS = $(SRCS:%.c=$(BUILD_DIR)/%.d) $(ASMS:%.S=$(BUILD_DIR)/%.d)

all: $(BIN)

$(BIN): $(OBJS) risc-v.lds Makefile
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(BUILD_DIR)/%.o: %.c Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.S Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

run: $(BIN)
	$(QEMU) $(QEMU_OPTS)

clean:
	rm -rf $(BUILD_DIR)

format:
	find . -iname "*.c" -o -iname "*.h" | xargs clang-format -i -style=Google
	@echo "✨ All files formatted."


.PHONY: all run clean format valgrind

-include $(DEPS)