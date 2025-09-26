# Makefile (Final Correction for Single Source File)

# 1. ONLY specify the final module object file.
obj-m := pubsub_module.o

# 2. REMOVE the pubsub_module-objs line, as it causes the linker error.
# pubsub_module-objs := pubsub_module.o  <-- REMOVE THIS LINE

BUILDROOT_DIR := ../..
KDIR := $(BUILDROOT_DIR)/output/build/linux-custom
COMPILER := $(BUILDROOT_DIR)/output/host/bin/i686-buildroot-linux-gnu-gcc

all:
	$(MAKE) -C $(KDIR) M=$$PWD
	$(MAKE) -C $(KDIR) M=$$PWD modules_install INSTALL_MOD_PATH=../../target
	
clean:
	rm -f *.o *.ko .*.cmd
	rm -f modules.order
	rm -f Module.symvers
	rm -f pubsub_module.mod.c