#!/bin/sh

# Load the core
insmod avirt_core.ko

# Load the additional Audio Paths
#insmod dummy/avirt_dummyap.ko
insmod loopback/avirt_loopbackap.ko

echo "Drivers Loaded!"
