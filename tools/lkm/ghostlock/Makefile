obj-m += ghostlock.o

# The module is written through the exploit page by page, so size matters:
# -Os plus no unwind tables (modules never use .eh_frame).
CFLAGS_ghostlock.o := -Os -fno-asynchronous-unwind-tables -fno-unwind-tables

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 LLVM=1 modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 LLVM=1 clean

.PHONY: all clean
