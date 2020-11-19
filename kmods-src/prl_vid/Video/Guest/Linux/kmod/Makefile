#######################################################
# Copyright (C) 1999-2019 Parallels International GmbH.
# All Rights Reserved.
# http://www.parallels.com
#######################################################

KVER ?= $(shell uname -r)
KERNEL_DIR ?= /lib/modules/$(KVER)/build
DRIVER_DIR ?= $(PWD)
DRIVER_DATE ?= $(shell date +%Y%m%d)
DRIVER_MAJOR ?= 1
DRIVER_MINOR ?= 7
DRIVER_VERSION ?= $(DRIVER_MAJOR).$(DRIVER_MINOR).0
DRIVER := prl_vid

$(info 1: $(KERNEL_DIR))
$(info 2: $(DRIVER_DIR))
$(info 3: $(DRIVER_DATE))
$(info 4: $(DRIVER_VERSION))

export KERNEL_DIR
export DRIVER_DIR
export PRL_DRM_CFLAGS

CFILES = prlvid.c prldrm.c prlvtg.c
ccflags-y += -DDRV_SHORT_NAME=\"$(DRIVER)\"
ccflags-y += -DDRV_DATE=\"$(DRIVER_DATE)\"
ccflags-y += -DDRV_VERSION=\"$(DRIVER_VERSION)\"
ccflags-y += -DDRV_MAJOR=$(DRIVER_MAJOR)
ccflags-y += -DDRV_MINOR=$(DRIVER_MINOR)
ccflags-y += -I$(KERNEL_DIR)/include
ccflags-y += -I$(obj)/../../../Interfaces
ccflags-y += -I$(obj)/../../../../../prl_tg/Toolgate/Interfaces
ccflags-y += -I$(obj)/../../../../../prl_tg/Toolgate/Guest/Linux/Interfaces
ccflags-y += -I$(obj)/../../../../../prl_tg/Toolgate/Guest/Linux/prl_tg
ccflags-y += $(PRL_DRM_CFLAGS)

obj-m += $(DRIVER).o
$(DRIVER)-objs += prlvid.o prldrm.o prlvtg.o

prl_vid: prl_drm_flags $(CFILES)
	$(info Start compile $(DRIVER)...)
	$(MAKE) -C $(KERNEL_DIR) M=$(DRIVER_DIR) SRCROOT=$(DRIVER_DIR) CC=$(CC) modules

prl_drm_flags:
	$(info Start checking drm core on kernel $(KERNEL_DIR)...)
	$(eval PRL_DRM_CFLAGS := $(shell cd ./test; ./test_drm_core.sh $(KERNEL_DIR)) )
	$(info Result extra flags: $(PRL_DRM_CFLAGS))

clean:
	$(info Start cleaning $(DRIVER)...)
	rm -rf *.o* *.ko *.mod* *symvers .tmp_versions .*.cmd *.ver modules.order

.PHONY: prl_drm_flags clean
