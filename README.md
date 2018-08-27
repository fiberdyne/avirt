Alsa Virtual Sound Driver
--------------------------
[![pipeline status](https://git.fiberdyne.com.au/JOSHANNE/kmodule_development/badges/master/pipeline.svg)](https://git.fiberdyne.com.au/JOSHANNE/kmodule_development/commits/master)

The ALSA Virtual Sound Driver (AVIRT) aims to provide a Linux kernel solution to the issue of audio routing in kernel-space, as well as security per-stream, and dynamic configuration of streams at the kernel level.

A top-level abstract dynamic audio driver is presented to the user-space via an ALSA middle-layer card. From there, respective low-level "real" audio drivers can subscribe to it as an "Audio Path".

The top-level driver is configured (currently) using module parameters, as is the norm for sound drivers in the Linux tree, however this will utilise a configfs configuration implementation in future.

A sample dummy Audio Path is provided as an example to show how a low-level audio driver would subscribe to AVIRT, and accept audio routing for playback.

## Building
### Out Of Tree
The kernel modules can be built either in-tree, or out-of-tree.
To build both AVIRT and the dummy Audio Path out-of-tree, use the following command:

```sh
$ CONFIG_AVIRT=m CONFIG_AVIRT_BUILDLOCAL=y CONFIG_AVIRT_DUMMYAP=m make -C /lib/modules/$(uname -r)/build/ M=$(pwd)
```

To build both AVIRT and the dummy Audio Path out-of-tree for [AGL](http://docs.automotivelinux.org/) (`aarch64` currently supported), use the [XDS](http://docs.automotivelinux.org/docs/devguides/en/dev/reference/xds/part-1/0_Abstract.html) build system together with the `make_agl.sh` script:

```sh
$ ./make_agl.sh ${XDS_SDK_ID}
```
### In tree
```
$ TODO
```

## Running
To run, we must load the kernel modules using the `loadDrivers.sh` script, which contains sample module parameters to AVIRT:
```sh
$ ./loadDrivers.sh
```
To unload the drivers use:
```sh
$ ./unload.sh
```

## TODO
 - Currently, playback only - implementing capture is WIP.
 - Rework module parameters into configfs configuration.
 - Create a loopback Audio Path for use with AVIRT, to demonstrate standard AGL soft-mixing capabilities.
 - Modify Fiberdyne DSP driver for use with AVRIT, to demonstrate DSP off-loading and hard-mixing capabilities via Xtensa HiFi2.

