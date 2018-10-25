#!/bin/sh

# Load the core
insmod snd-avirt-core.ko

# Load the additional Audio Paths
insmod dummy/snd-avirt-ap-dummy.ko
insmod loopback/snd-avirt-ap-loopback.ko

# Run the test script
./scripts/test_configfs.sh

echo "Drivers Loaded!"
