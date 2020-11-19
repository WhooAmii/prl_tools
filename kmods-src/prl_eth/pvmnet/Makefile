#######################################################
# Copyright (C) 1999-2016 Parallels International GmbH.
# All Rights Reserved.
# http://www.parallels.com
#######################################################
#
# To make driver for current kernel:
#
#        make
#
# To make driver for other kernel:
#
#        make KERNEL_DIR=<PATH_TO_KERNEL>
#

KVER ?= $(shell uname -r)
KERNEL_DIR ?= /lib/modules/$(KVER)/build

DRIVER := prl_eth
DRIVER_DIR ?= $(PWD)

export DRIVER_DIR

EXTRA_CFLAGS +=  -I$(DRIVER_DIR)

ifeq "$(wildcard $(DRIVER_DIR)/Makefile.local)" "$(DRIVER_DIR)/Makefile.local"
  include $(DRIVER_DIR)/Makefile.local
endif

obj-m := $(DRIVER).o
$(DRIVER)-objs := pvmnet.o

all:
	make -C $(KERNEL_DIR) M=$(PWD) CC=$(CC)

clean:
	make -C $(KERNEL_DIR) M=$(PWD) CC=$(CC) clean
	rm -f Module*.symvers

distclean: clean
	rm -f *~
