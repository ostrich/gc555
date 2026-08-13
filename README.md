# AVerMedia Live Gamer BOLT GC555 / Live Gamer 4K GC573 Linux driver

This repository contains a Linux kernel driver for the AVerMedia Live Gamer
BOLT GC555 Thunderbolt capture device and the AVerMedia Live Gamer 4K GC573
PCIe capture device. It provides V4L2 video capture, ALSA audio capture, and
HDMI passthrough. The kernel module is built entirely from source and does not
depend on AVerMedia's proprietary kernel objects.

The GC555 and GC573 share the same board design, FPGA, and ITE HDMI chip
register maps. The GC573 replaces the IT6805 receiver with an IT68051TE and
the IT6664 splitter with an IT6663FN. Both are recognized by the same
identity checks, and the single module drives either card based on the PCI
subsystem ID.

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
- Replacing the passthrough display with one that has a different EDID.
- Two-, six-, and eight-channel `S16_LE` and packed `S24_3LE` ALSA capture at
  32, 44.1, and 48 kHz.
- Stereo `S16_LE` capture from the 3.5 mm line input at 48 kHz.
- Stereo LPCM passthrough at 32, 44.1, and 48 kHz.
- Per-LED RGB lighting control through the Linux multicolor LED class.
- Read-only access to the EDID advertised on HDMI IN through sysfs.
- HDMI signal reconnects and source mode changes.
- Module reload, suspend and resume with the device retained, and Thunderbolt
  surprise removal.

## Implemented but not yet tested

The following paths are present in the driver but have not been validated with
physical hardware:

- 1080i50 and 1080i60 capture.
- 1080p240 HDMI passthrough.
- HDCP 1.x and 2.x protected-source handling with an ordinary downstream
  sink.
- Multichannel and 20- or 24-bit LPCM passthrough.
- Some less common HDR and `P010` resolution and refresh-rate combinations.

## Not supported

- Changing between RGB and YCbCr sampling while a capture stream is running.
- `UYVY` video output.
- Host audio capture other than two-, six-, or eight-channel `S16_LE` or
  `S24_3LE`; unsupported combinations include 20-bit capture and other channel
  counts.
- Compressed-audio passthrough.
- HDCP repeater topology processing.
- Firmware updates.
- Protected host capture. Video frames are blanked when the input is protected
  or its protection state cannot be determined.

## Building and loading

Install the headers for the running kernel, a C compiler, and GNU Make.

The module depends on `snd`, `snd-pcm`, `videodev`, `v4l2-dv-timings`,
`videobuf2-common`, `videobuf2-v4l2`, and `videobuf2-dma-sg`. RGB lighting is
enabled when the kernel provides `led-class-multicolor`; capture works without
it.

Build and load the driver with:

```sh
make
sudo modprobe snd-pcm
sudo modprobe v4l2-dv-timings
sudo modprobe videobuf2-v4l2
sudo modprobe videobuf2-dma-sg
sudo insmod ./gc555.ko
```

To omit lighting support even when the kernel provides the multicolor LED
class, build with `make CONFIG_LEDS_CLASS_MULTICOLOR=n`.

The module registers one V4L2 capture device and one ALSA card with separate
HDMI and analog line-in PCM capture devices. Device numbers are not fixed; use
the following commands to locate them:

```sh
v4l2-ctl --list-devices
arecord -l
```

The packaged udev rule also creates a persistent V4L2 link using the device's
PCIe serial number:

```text
/dev/v4l/by-id/pci-AVerMedia_Live_Gamer_BOLT_GC555_<serial>-video-index0
/dev/v4l/by-id/pci-AVerMedia_Live_Gamer_4K_GC573_<serial>-video-index0
```

Close all video and audio clients before unloading the module:

```sh
sudo rmmod gc555
```
