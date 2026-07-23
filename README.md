# AVerMedia Live Gamer BOLT GC555 Linux driver

This repository contains a Linux kernel driver for the AVerMedia Live Gamer
BOLT GC555 Thunderbolt capture device. It provides V4L2 video capture, ALSA
audio capture, and HDMI passthrough. The kernel module is built entirely from
source and does not depend on AVerMedia's proprietary kernel objects.

Only the GC555 is supported. Other AVerMedia capture devices are not supported.

## Supported and tested

- Native-resolution V4L2 capture. Validated input modes include 480p, 576p,
  1080p240, 1440p144, 3440x1440p100, 3840x2160p60, and common PC resolutions.
- `YUYV`, `NV12`, `BGR24`, `RGB32`, and `P010` output on the modes validated
  for each format.
- RGB input and YCbCr 4:4:4, 4:2:2, and 4:2:0 input.
- HDR10 capture as `P010` and HDR passthrough through 3840x2160p60.
- HDMI passthrough and headless capture.
- HDMI OUT disconnect and reconnect at 1080p60, 1440p144, and
  3840x2160p60.
- Two- and eight-channel `S16_LE` ALSA capture at 32, 44.1, and 48 kHz.
- Stereo LPCM passthrough at 32, 44.1, and 48 kHz.
- HDMI signal reconnects and source mode changes.
- Module reload, suspend and resume with the device retained, and Thunderbolt
  surprise removal.

## Implemented but not yet tested

The following paths are present in the driver but have not been validated with
physical hardware:

- 1080i50 and 1080i60 capture.
- 1080p240 HDMI passthrough.
- Replacing the passthrough display with one that has a different EDID.
- HDCP 1.x and 2.x handling with a protected source.
- Multichannel and 20- or 24-bit LPCM passthrough.
- Some less common HDR and `P010` resolution and refresh-rate combinations.

## Not supported

- Scaling, cropping, or flipping. Video is captured at the source resolution.
- Changing between RGB and YCbCr sampling while a capture stream is running.
- `UYVY` video output.
- Host audio capture other than two- or eight-channel `S16_LE`; this excludes
  5.1-channel and 20- or 24-bit capture.
- Compressed-audio passthrough.
- Analog line input.
- RGB lighting control.
- Firmware updates.
- Protected host capture. Video frames are blanked when the input is protected
  or its protection state cannot be determined.

## Building and loading

Install the headers for the running kernel, a C compiler, and GNU Make.

The module depends on `snd`, `snd-pcm`, `videodev`, `videobuf2-common`,
`videobuf2-v4l2`, and `videobuf2-dma-sg`.

Build and load the driver with:

```sh
make
sudo modprobe snd-pcm videobuf2-v4l2 videobuf2-dma-sg
sudo insmod ./gc555.ko
```

The module registers one V4L2 capture device and one ALSA card. Their device
numbers are not fixed; use the following commands to locate them:

```sh
v4l2-ctl --list-devices
arecord -l
```

Close all video and audio clients before unloading the module:

```sh
sudo rmmod gc555
```
