# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_VIDEO_GC555) += gc555.o

gc555-y := gc555-core.o gc555-bridge.o gc555-fpga.o gc555-link.o \
	gc555-i2c.o gc555-edid.o \
	gc555-dma.o gc555-video-dma.o gc555-video.o gc555-audio.o \
	gc555-it6664-core.o \
	gc555-it6664-tx.o \
	gc555-it6805-core.o

ifneq ($(filter y m,$(CONFIG_LEDS_CLASS_MULTICOLOR)),)
gc555-y += gc555-led.o
ccflags-y += -DGC555_HAS_LED_CLASS
endif
