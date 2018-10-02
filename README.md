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

## Running

Firstly, install the resulting kernel modules to the appropriate modules
directory, and run`depmod`. For example, in AGL:

```sh
cp avirt_core.ko avirt_dummyap.ko /lib/modules/4.14.0-yocto-standard/
depmod
```

Now we can load the modules:

```sh
modprobe avirt_core
modprobe avirt_dummyap
```

We must now configure AVIRT. We can do this with the test script:

```sh
source scripts/test_configfs.sh
```

We can see the newly created streams by using the `aplay` utility. For example:

```sh
aplay -l
...
card 2: avirt [avirt], device 0: multimedia [multimedia]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 2: avirt [avirt], device 1: navigation [navigation]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 2: avirt [avirt], device 2: emergency [emergency]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
```

## 4A Integration

AVIRT has been integrated with [4a-softmixer](https://github.com/iotbzh/4a-softmixer)
and [4a-hal-generic](https://github.com/iotbzh/4a-hal-generic) to provide a smooth
transition from the existing aloop implementation to the future AVIRT loopback implementation.

### User-space Library

The user-space library [libavirt](https://github.com/fiberdyne/libavirt) can be used to configure AVIRT from within a given AGL 4A binding.

### Hardmixer

A new 4A mixer binding has been developed to demonstrate the capabilities of the
Fiberdyne DSP mixer operating on the HiFi2 core on-board the Renesas R-Car M3 M3ULCB
AGL reference platform. This is called the 'hardmixer', and is a faster, lower
latency alternative to the softmixer.

## TODO

- Currently, playback only - implementing capture is WIP.
- Create a loopback Audio Path for use with AVIRT, to demonstrate standard AGL soft-mixing capabilities.
