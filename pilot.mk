# Consumer integration: include this file, then use PILOT_CPPFLAGS/PILOT_LDLIBS.
ifndef PILOT_ROOT
PILOT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
endif
PILOT_TARGET ?= arm
PILOT_LINK ?= static
PILOT_LIBDIR ?= $(if $(wildcard $(PILOT_ROOT)/lib/$(PILOT_TARGET)/libpilot.a),$(PILOT_ROOT)/lib/$(PILOT_TARGET),$(PILOT_ROOT)/build/$(PILOT_TARGET))
ifeq ($(PILOT_TARGET),arm)
include $(PILOT_ROOT)/toolchain/arm.mk
PILOT_PRIVATE_LIBS := -L$(PILOT_ROOT)/vendor/dbus/lib -ldbus-1 -pthread
else ifeq ($(PILOT_TARGET),host)
CC := gcc
PILOT_PRIVATE_LIBS := $(shell pkg-config --libs dbus-1) -pthread
else
$(error PILOT_TARGET must be arm or host)
endif
PILOT_CPPFLAGS := -I$(PILOT_ROOT)/include
ifeq ($(PILOT_LINK),static)
PILOT_LIBRARY := $(PILOT_LIBDIR)/libpilot.a
PILOT_LDLIBS := $(PILOT_LIBRARY) $(PILOT_PRIVATE_LIBS)
else ifeq ($(PILOT_LINK),shared)
PILOT_LIBRARY := $(PILOT_LIBDIR)/libpilot.so.1
PILOT_LDLIBS := -L$(PILOT_LIBDIR) -Wl,-rpath-link,$(PILOT_ROOT)/vendor/dbus/lib -lpilot
else
$(error PILOT_LINK must be static or shared)
endif
