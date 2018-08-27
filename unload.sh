#!/bin/sh

rm_module() {
        lsmod |grep "^$1\>" && rmmod $1 || true
}

rm_module avirt_dummyap
rm_module avirt_core
echo "Drivers Removed!"