#!/bin/sh

rmdir /config/snd-avirt/streams/playback_*
rmdir /config/snd-avirt/streams/capture_*

rm_module() {
        lsmod |grep "^$1\>" && rmmod $1 || true
}

rm_module snd_avirt_ap_loopback
rm_module snd_avirt_ap_dummy
rm_module snd_avirt_core
echo "Drivers Removed!"
