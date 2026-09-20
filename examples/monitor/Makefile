PILOT_ROOT ?= $(abspath ../..)
include $(PILOT_ROOT)/toolchain/arm.mk
CFLAGS ?= -O2 -Wall -Wextra -Werror
CPPFLAGS += -I$(PILOT_ROOT)/include
LDFLAGS += -Wl,-z,noexecstack,-z,relro,-z,now
PILOT_LIB ?= $(PILOT_ROOT)/build/arm/libpilot.a

.PHONY: all clean
all: pilot-monitor
pilot-monitor: main.c $(PILOT_LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) main.c $(PILOT_LIB) $(DBUS_LIBS) -pthread -o $@
clean:
	rm -f -- pilot-monitor
