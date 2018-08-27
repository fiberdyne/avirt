#!/bin/bash

# Get SDK path
sdk_id=$1 # first arg must be XDS_SDK_ID
shift 1
long_sdkpath=$(xds-cli sdks get $sdk_id | grep Path)
sdkpath=${long_sdkpath:4}

# Build
/opt/AGL/bin/xds-cli exec --config xds-project.conf --					\
	LDFLAGS= CONFIG_AVIRT=m CONFIG_AVIRT_BUILDLOCAL=y CONFIG_AVIRT_DUMMYAP=m	\
	make -C $sdkpath/sysroots/aarch64-agl-linux/usr/src/kernel M=$(pwd) $@
