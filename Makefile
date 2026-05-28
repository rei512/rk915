# SPDX-License-Identifier: GPL-2.0-or-later

ifeq ($(CONFIG_RK915),)
CONFIG_RK915=m
endif

ccflags-y += -I$(src)/inc \
             -I$(src)/shared

rk915-objs := src/main.o \
	      src/hal.o \
	      src/umac_if.o \
	      src/rpu_if.o \
	      src/tx.o \
	      src/rx.o \
	      src/beacon.o \
	      src/p2p.o \
	      src/procfs.o \
	      src/utils.o \
	      src/vif.o \
	      src/wow.o \
	      src/hal_io.o \
	      src/platform.o \
	      src/firmware.o \
	      src/init.o \
	      src/sdio.o

obj-$(CONFIG_RK915) += rk915.o

ifeq ($(KERNELRELEASE),)

all:
	$(MAKE) -C $(KROOT) M=$(PWD) modules
clean:
	$(MAKE) -C $(KROOT) M=$(PWD) clean

endif
