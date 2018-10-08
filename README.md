## Alsa Virtual Sound Driver

The ALSA Virtual Sound Driver (AVIRT) aims to provide a Linux kernel solution to the issue of audio routing in kernel-space, as well as security per-stream, and dynamic configuration of streams at the kernel level.

A top-level abstract dynamic audio driver is presented to the user-space via an ALSA middle-layer card. From there, respective low-level "real" audio drivers can subscribe to it as an "Audio Path".

The top-level driver is configured (currently) using module parameters, as is the norm for sound drivers in the Linux tree, however this will utilise a configfs configuration implementation in future.

A sample dummy Audio Path is provided as an example to show how a low-level audio driver would subscribe to AVIRT, and accept audio routing for playback.

Currently, the Fiberdyne DSP hardmixer is supported on the Renesas R-Car M3 AGL
reference platform, and a default loopback softmixer is in development.

## Building

### Out Of Tree

The kernel modules can be built either in-tree, or out-of-tree.
To build both AVIRT and the dummy Audio Path out-of-tree, use the following command:

```sh
$ make all
```

If building for [AGL](http://docs.automotivelinux.org/), use the [XDS](http://docs.automotivelinux.org/docs/devguides/en/dev/reference/xds/part-1/0_Abstract.html) build system together with the `make_agl.sh` script:

```sh
$ ./make_agl.sh ${XDS_SDK_ID}
```

### In tree

To build in tree, use the [Fiberdyne Linux fork](https://github.com/fiberdyne/linux), which will automatically clone the AVIRT Driver and required AudioPath modules to the `drivers/staging` directory. You can then turn AVIRT Support on by setting to `<M>`. The drivers can be found here:

```
$ make menuconfig

# Navigate to: Device Drivers--->Staging Drivers--->AVIRT Support
```

Finally build the kernel with the configuration selected by making.

```
$ make
```

## TODO

- Currently, playback only - implementing capture is WIP.
- Create a loopback Audio Path for use with AVIRT, to demonstrate standard AGL soft-mixing capabilities.
