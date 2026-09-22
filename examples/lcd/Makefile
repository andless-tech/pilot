PILOT_ROOT ?= $(abspath ../..)
include $(PILOT_ROOT)/pilot.mk
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror
OUTPUT ?= pilot-lcd
.PHONY: all clean
all: $(OUTPUT)
$(OUTPUT): main.c $(PILOT_LIBRARY) Makefile $(PILOT_ROOT)/pilot.mk
	$(CC) $(PILOT_CPPFLAGS) $(CFLAGS) main.c $(PILOT_LDLIBS) -o $@
clean:
	rm -f -- $(OUTPUT)
