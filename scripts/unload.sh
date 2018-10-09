#!/bin/sh

rmdir /config/avirt/streams/playback_*
rmdir /config/avirt/streams/capture_*

rm_module() {
        lsmod |grep "^$1\>" && rmmod $1 || true
}

rm_module avirt_loopbackap
rm_module avirt_dummyap
rm_module avirt_core
echo "Drivers Removed!"
