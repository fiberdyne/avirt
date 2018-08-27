# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_AVIRT) += avirt_core.o
avirt_core-y := core.o
avirt_core-y += alsa.o
avirt_core-y += alsa-pcm.o

ifeq ($(CONFIG_AVIRT_BUILDLOCAL),)
	CCFLAGS_AVIRT := "drivers/staging/"
else
	CCFLAGS_AVIRT := "$(PWD)/../"
endif

ccflags-y += -I${CCFLAGS_AVIRT}

$(info $(KERNELRELEASE))
obj-$(CONFIG_AVIRT_DUMMYAP)	+= dummy/
