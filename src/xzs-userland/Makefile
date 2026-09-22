CLANG := /usr/bin/xcrun clang

TARGETS := launchd sh hello args
ROOTFS_DIR := ../../rootfs/xzs-root

ASFLAGS := -target arm64-apple-macos11 -c
CFLAGS := -target arm64-apple-macos11 -ffreestanding -fno-stack-protector \
	-fno-builtin -Os -c
LDFLAGS := -target arm64-apple-macos11 -nostdlib \
	-Wl,-static \
	-Wl,-e,_start \
	-Wl,-pagezero_size,0x100000000 \
	-Wl,-segalign,0x4000

all: $(TARGETS)

%.o: %.S
	$(CLANG) $(ASFLAGS) $< -o $@

%.o: %.c
	$(CLANG) $(CFLAGS) $< -o $@

launchd: launchd.o
	$(CLANG) $(LDFLAGS) $< -o $@

sh: sh.o shell.o shell_line.o svc.o
	$(CLANG) $(LDFLAGS) sh.o shell.o shell_line.o svc.o -o $@

hello: hello.o
	$(CLANG) $(LDFLAGS) $< -o $@

args: args.o
	$(CLANG) $(LDFLAGS) $< -o $@

install-launchd: launchd
	cp launchd $(ROOTFS_DIR)/sbin/launchd

install-sh: sh hello args
	cp sh $(ROOTFS_DIR)/bin/sh
	cp hello $(ROOTFS_DIR)/bin/hello
	cp args $(ROOTFS_DIR)/bin/args

install: install-launchd install-sh

clean:
	rm -f *.o $(TARGETS)

.PHONY: all install install-launchd install-sh clean
