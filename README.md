## Alsa Virtual Sound Driver

The ALSA Virtual Sound Driver (AVIRT) aims to provide a Linux kernel solution to the issue of audio routing in kernel-space, as well as security per-stream, and dynamic configuration of streams at the kernel level.

A top-level abstract dynamic audio driver is presented to the user-space via an ALSA middle-layer card. From there, respective low-level "real" audio drivers can subscribe to it as an "Audio Path".

The top-level driver is configured (currently) using module parameters, as is the norm for sound drivers in the Linux tree, however this will utilise a configfs configuration implementation in future.

A sample dummy Audio Path is provided as an example to show how a low-level audio driver would subscribe to AVIRT, and accept audio routing for playback.

Currently, the Fiberdyne DSP hardmixer is supported on the Renesas R-Car M3 AGL
reference platform, and a default loopback softmixer is in development.

## TODO

- Currently, playback only - implementing capture is WIP.
- Create a loopback Audio Path for use with AVIRT, to demonstrate standard AGL soft-mixing capabilities.
