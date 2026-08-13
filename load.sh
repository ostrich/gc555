#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Load the gc555/gc573 kernel module and print bring-up diagnostics.

set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ "$(id -u)" -ne 0 ]; then
	echo "Re-run as root (sudo ./load.sh)" >&2
	exit 1
fi

modprobe snd-pcm
modprobe videodev
modprobe v4l2-dv-timings
modprobe videobuf2-common
modprobe videobuf2-v4l2
modprobe videobuf2-dma-sg

rmmod gc555 2>/dev/null || true

insmod "$project_dir/gc555.ko"

echo "--- module loaded, dmesg tail:"
dmesg | tail -40

echo "--- V4L2 devices:"
command -v v4l2-ctl >/dev/null && v4l2-ctl --list-devices || echo "(v4l2-utils not installed)"
echo "--- ALSA cards:"
command -v arecord >/dev/null && arecord -l || echo "(alsa-utils not installed)"