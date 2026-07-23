# AVerMedia Live Gamer BOLT GC555 Linux Driver

Source-owned Linux driver for the AVerMedia Live Gamer BOLT GC555 Thunderbolt
capture device. It provides V4L2 video capture, ALSA audio capture, and HDMI
passthrough without proprietary kernel objects. Other AVerMedia devices are not
supported.

## Supported

- Native-resolution V4L2 capture from 480p/576p and common PC modes through
  1080p240, 1440p144, 3440x1440p100, and 3840x2160p60. Output formats are
  YUYV, NV12, BGR24, RGB32, and P010 on their validated modes.
- RGB and YCbCr 4:4:4, 4:2:2, and 4:2:0 input, including representative HDR10
  P010 capture and passthrough through 3840x2160p60.
- HDMI passthrough, headless capture, and HDMI OUT disconnect/reconnect at
  1080p60, 1440p144, and 3840x2160p60.
- Two- and eight-channel `S16_LE` ALSA capture at 32, 44.1, and 48 kHz, plus
  stereo LPCM passthrough at those rates.
- Signal reconnect, mode changes, reload, retained-device suspend/resume, and
  Thunderbolt surprise removal.

## Implemented But Untested

- 1080i50/60 capture.
- Physical 1080p240 passthrough and replacement-display EDID handling.
- HDCP 1.x/2.x classification and lawful passthrough with a real protected
  source.
- Multichannel and 20/24-bit LPCM passthrough, plus some enumerated HDR/P010
  timing permutations.

## Not Implemented

- Scaling, cropping, or flipping; capture is native-resolution only.
- Seamless live switching between RGB and YCbCr sampling formats.
- UYVY video output, 20/24-bit or 5.1 host audio capture, and compressed-audio
  passthrough.
- Analog line input, RGB lighting control, or firmware updating.
- Protected host capture; protected or unknown input is intentionally blanked.

## Build

Install the headers for the running kernel, a C compiler, and GNU Make.

The module depends on `snd`, `snd-pcm`, `videodev`, `videobuf2-common`,
`videobuf2-v4l2`, and `videobuf2-dma-sg`.

Build and load it with:

```sh
make
sudo modprobe snd-pcm videobuf2-v4l2 videobuf2-dma-sg
sudo insmod gc555.ko
```

The module registers one V4L2 capture device and one ALSA card. Locate them
with `v4l2-ctl --list-devices` and `arecord -l`; device numbers are not fixed.

Unload the module after closing all video and audio clients:

```sh
sudo rmmod gc555
```
