ifndef PILOT_ROOT
PILOT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
endif
TOOLCHAIN_NAME := arm-rockchip830-linux-uclibcgnueabihf
TOOLCHAIN_HASH := $(word 1,$(shell cat $(PILOT_ROOT)/toolchain/$(TOOLCHAIN_NAME).tar.gz.sha256 2>/dev/null))
PILOT_TOOLCHAIN_CACHE ?= $(HOME)/.cache/pilot/toolchains
TOOLCHAIN_ROOT ?= $(PILOT_TOOLCHAIN_CACHE)/$(TOOLCHAIN_HASH)/$(TOOLCHAIN_NAME)
CROSS_COMPILE ?= $(TOOLCHAIN_ROOT)/bin/$(TOOLCHAIN_NAME)-
CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar
STRIP := $(CROSS_COMPILE)strip
READELF := $(CROSS_COMPILE)readelf
