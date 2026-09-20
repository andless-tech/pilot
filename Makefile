TARGET ?= arm
PILOT_ROOT := $(CURDIR)
BUILD := build/$(TARGET)$(if $(SANITIZE),-sanitize,)
ifeq ($(TARGET),host)
CC := gcc
AR := ar
READELF := readelf
DBUS_CFLAGS := $(shell pkg-config --cflags dbus-1)
DBUS_LIBS := $(shell pkg-config --libs dbus-1)
else ifeq ($(TARGET),arm)
include toolchain/arm.mk
else
$(error TARGET must be arm or host)
endif

CPPFLAGS += -Iinclude $(DBUS_CFLAGS)
CFLAGS += -std=c11 -O2 -g -fPIC -Wall -Wextra -Werror -MMD -MP
LDFLAGS += -Wl,-z,noexecstack,-z,relro,-z,now
LDLIBS := $(DBUS_LIBS) -pthread
ifneq ($(SANITIZE),)
CFLAGS += -O1 -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif
OBJECTS := $(BUILD)/pilot.o $(BUILD)/api.o

.PHONY: all check-generated generate test verify install package clean example
all: $(BUILD)/libpilot.a $(BUILD)/libpilot.so.0 $(BUILD)/pilot-monitor

$(BUILD):
	mkdir -p $@
$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/libpilot.a: $(OBJECTS)
	$(AR) rcs $@ $^
$(BUILD)/libpilot.so.0: $(OBJECTS)
	$(CC) -shared $(LDFLAGS) -Wl,-soname,libpilot.so.0 -Wl,--no-undefined -o $@ $^ $(LDLIBS)
	ln -sf libpilot.so.0 $(BUILD)/libpilot.so
$(BUILD)/pilot-monitor: examples/monitor/main.c $(BUILD)/libpilot.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(BUILD)/libpilot.a $(LDLIBS) -o $@
$(BUILD)/test-pilot: tests/test_pilot.c $(BUILD)/libpilot.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(BUILD)/libpilot.a $(LDLIBS) -o $@
$(BUILD)/test-disconnect: tests/test_disconnect.c $(BUILD)/libpilot.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(BUILD)/libpilot.a $(LDLIBS) -o $@
generate:
	python3 scripts/generate.py
check-generated:
	python3 scripts/generate.py --check
test: check-generated
ifeq ($(TARGET),host)
	$(MAKE) TARGET=host SANITIZE=$(SANITIZE) $(BUILD)/test-pilot $(BUILD)/test-disconnect
	g++ -std=c++11 -Wall -Wextra -Werror -Iinclude -fsyntax-only tests/public_headers.cpp
	dbus-run-session -- $(BUILD)/test-pilot
	python3 tests/test_bus_restart.py $(BUILD)/test-disconnect
else
	$(MAKE) TARGET=host test
endif
verify: all check-generated
	$(READELF) -h $(BUILD)/pilot-monitor
	$(READELF) -l $(BUILD)/pilot-monitor
	$(READELF) -d $(BUILD)/libpilot.so.0
PREFIX ?= /usr/local
install: all
	install -d $(DESTDIR)$(PREFIX)/include/pilot $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/bin
	install -m 644 include/pilot/*.h $(DESTDIR)$(PREFIX)/include/pilot/
	install -m 644 $(BUILD)/libpilot.a $(DESTDIR)$(PREFIX)/lib/
	install -m 755 $(BUILD)/libpilot.so.0 $(DESTDIR)$(PREFIX)/lib/
	ln -sf libpilot.so.0 $(DESTDIR)$(PREFIX)/lib/libpilot.so
	install -m 755 $(BUILD)/pilot-monitor $(DESTDIR)$(PREFIX)/bin/
package: all check-generated
	python3 scripts/package.py $(TARGET)
clean:
	rm -rf -- $(BUILD)
-include $(OBJECTS:.o=.d)
