# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_AVIRT) += avirt_core.o
avirt_core-y := core.o
avirt_core-y += alsa-pcm.o
avirt_core-y += configfs.o

ifeq ($(CONFIG_AVIRT_BUILDLOCAL),)
	CCFLAGS_AVIRT := "drivers/staging/"
else
	CCFLAGS_AVIRT := "$(PWD)"
endif

ccflags-y += -I${CCFLAGS_AVIRT}

$(info $(KERNELRELEASE))
obj-$(CONFIG_AVIRT_DUMMYAP)	+= dummy/
obj-$(CONFIG_AVIRT_LOOPBACKAP)	+= loopback/

###
# For out-of-tree building
###
ifdef PKG_CONFIG_SYSROOT_DIR
	# AGL SDK kernel src path
	KERNEL_SRC=$(PKG_CONFIG_SYSROOT_DIR)/usr/src/kernel
else
	# General Linux distro kernel src path
	KERNEL_SRC=/lib/modules/$(shell uname -r)/build
endif

all:
	CONFIG_AVIRT=m CONFIG_AVIRT_BUILDLOCAL=y \
	CONFIG_AVIRT_DUMMYAP=m \
	CONFIG_AVIRT_LOOPBACKAP=m \
	make -C $(KERNEL_SRC) M=$(PWD)

clean:
	make -C $(KERNEL_SRC) M=$(PWD) clean

