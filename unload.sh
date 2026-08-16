#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ "$(id -u)" -ne 0 ]; then
	echo "Re-run as root (sudo ./unload.sh)" >&2
	exit 1
fi

rmmod gc555
echo "gc555 module unloaded"