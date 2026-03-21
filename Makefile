OUTPUT = t51

AMD64_LINKER =
AMD64_COMPILER =
I386_LINKER = -m elf_i386
I386_COMPILER = -m32

ARCH = AMD64

LINKER = ld -no-pie -n --gc-sections $($(ARCH)_LINKER)

COMPILER_CC = cc
COMPILER_STD = -std=c99
COMPILER_STD += -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-parentheses -Wno-strict-aliasing

COMPILER = COMPILER_CC
COMPILER += COMPILER_STD
COMPILER += -fno-pic
COMPILER += -fno-pie
COMPILER += -nostdlib -fomit-frame-pointer -fno-stack-protector -ffreestanding -fno-asynchronous-unwind-tables -fdata-sections -ffunction-sections -Qn
COMPILER += $($(ARCH)_COMPILER)
COMPILER += -c
COMPILER += -Iinclude

debug: CFLAGS = -O1 -g
debug: _all
small: CFLAGS = -s -Os
small: LINKER += -z nosectionheader
small: _all
release: CFLAGS = -O3 -ffast-math
release: LINKER += -z nosectionheader
release: _all

_all: _compile _clean

_compile:
ifeq ($(use_dpdk),yes)
	$(COMPILER_CC) $(COMPILER_STD) src/main.c -I/usr/include/dpdk -I/usr/include/x86_64-linux-gnu/dpdk/ -lrte_eal -lrte_ethdev -lrte_net_ixgbe -Dset_use_dpdk=1 -o $(OUTPUT)
else
	$(COMPILER) $(CFLAGS) src/main.c -o main.c.o && \
	$(LINKER) main.c.o -o $(OUTPUT) || true
endif

_clean:
	rm -f main.c.o
