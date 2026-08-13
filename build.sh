#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Build wrapper for the gc555/gc573 kernel module.
#
# The Linux kbuild system cannot build external modules whose path contains
# spaces (the M= variable is split on whitespace).  If the project lives in a
# path without spaces you can build directly with:
#
#     make modules LLVM=1
#
# Otherwise this script stages a copy in a space-free directory, builds it
# there, and copies gc555.ko back into the project root.

set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${BUILD_DIR:-/tmp/gc555-kbuild}

case "$project_dir" in
	*" "*) : ;;
	*) exec make -C "$project_dir" modules "$@" ;;
esac

rm -rf "$build_dir"
mkdir -p "$build_dir"

cp -a \
	Kbuild Kconfig Makefile \
	gc555*.c gc555*.h \
	COPYING \
	"$build_dir"/

make -C /lib/modules/"$(uname -r)"/build \
	M="$build_dir" CONFIG_VIDEO_GC555=m modules "$@"

cp "$build_dir"/gc555.ko "$project_dir"/gc555.ko
echo "Built $project_dir/gc555.ko"