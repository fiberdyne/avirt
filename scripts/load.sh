#!/bin/sh

# Load the core
insmod avirt_core.ko

# Load the additional Audio Paths
insmod dummy/avirt_dummyap.ko
insmod loopback/avirt_loopbackap.ko

# Run the test script
./scripts/test_configfs.sh

echo "Drivers Loaded!"
