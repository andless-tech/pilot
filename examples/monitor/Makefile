PILOT_ROOT ?= $(abspath ../..)
include $(PILOT_ROOT)/pilot.mk
CFLAGS ?= -O2 -Wall -Wextra -Werror
CPPFLAGS += $(PILOT_CPPFLAGS)
LDFLAGS += -Wl,-z,noexecstack,-z,relro,-z,now
OUTPUT ?= pilot-monitor

.PHONY: all clean
all: $(OUTPUT)
$(OUTPUT): main.c $(PILOT_LIBRARY) Makefile $(PILOT_ROOT)/pilot.mk
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) main.c $(PILOT_LDLIBS) -o $@
clean:
	rm -f -- $(OUTPUT)
