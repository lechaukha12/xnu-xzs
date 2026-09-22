CC := clang
LD := /opt/homebrew/bin/ld.lld
OBJCOPY := /opt/homebrew/opt/llvm/bin/llvm-objcopy
OBJDUMP := /opt/homebrew/opt/llvm/bin/llvm-objdump

CFLAGS := -target aarch64-none-elf -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -fno-pic -mgeneral-regs-only -mstrict-align -O2 -Wall -I.
ASFLAGS := -target aarch64-none-elf -ffreestanding
LDFLAGS := -T linker.ld -nostdlib

ROOTFS_IMG := ../../artifacts/builds/xzs-rootfs.img
ROOTFS_EXPECTED_SHA256 := b24cffc22b05a4bf54c2bfad1c79452f587ca4c1646390951de0696272504cec

OBJS := start.o main.o uart.o adt.o fb.o rootfs_blob.o
TARGET := bootshim

all: $(TARGET).bin $(TARGET).dis

rootfs_blob.o: rootfs_blob.S $(ROOTFS_IMG)
	@if [ ! -f $(ROOTFS_IMG) ]; then \
		echo "ERROR: $(ROOTFS_IMG) missing!"; exit 1; \
	fi
	@ACTUAL_SHA=$$(shasum -a 256 $(ROOTFS_IMG) | awk '{print $$1}'); \
	if [ "$$ACTUAL_SHA" != "$(ROOTFS_EXPECTED_SHA256)" ]; then \
		echo "ERROR: $(ROOTFS_IMG) SHA256 mismatch! Expected $(ROOTFS_EXPECTED_SHA256), got $$ACTUAL_SHA"; exit 1; \
	fi
	$(CC) $(ASFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(ASFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(TARGET).dis: $(TARGET).elf
	$(OBJDUMP) -d $< > $@

clean:
	rm -f *.o *.elf *.bin *.dis

.PHONY: all clean
