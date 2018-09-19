#!/bin/bash

# Sample module parameters
params="\
playback_num=4 \
playback_chans=2,4,1,1 \
playback_names=radio,media,nav,phone \
capture_num=1 \
capture_chans=2 \
capture_names=voice"

# Load the virtual driver
insmod avirt_core.ko "$params"

# Load the additional audio path
#insmod dummy/avirt_dummyap.ko
insmod loopback/avirt_loopbackap.ko

echo "Drivers Loaded!"
