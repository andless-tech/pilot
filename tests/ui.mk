# Explicit source path keeps the public SDK independent of the firmware tree.
DISPLAY_UI ?= /home/xjxwsl/workspace/endless/quicore/project/app/display_ui
UI_OUT ?= /tmp/pilot-ui-integration
UI_CFLAGS = -std=c11 -Wall -Wextra -Werror -O1 -g -Iinclude -I$(DISPLAY_UI)/src/app $(shell pkg-config --cflags dbus-1)
UI_CFLAGS += -DPILOT_UI_TEST_SESSION -DEXTENSION_TEST_SESSION
UI_LIBS = $(shell pkg-config --libs dbus-1) -lpthread -lm
ifneq ($(SANITIZE),)
UI_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
UI_LIBS += -fsanitize=address,undefined
endif
test-ui:
	cmp src/private/ui_wire.h $(DISPLAY_UI)/src/app/extension_wire.h
	$(CC) $(UI_CFLAGS) tests/test_ui_queue.c src/runtime.c $(UI_LIBS) -o $(UI_OUT)-queue
	$(UI_OUT)-queue
	$(CC) $(UI_CFLAGS) tests/test_ui.c src/ui.c src/runtime.c $(DISPLAY_UI)/src/app/extension_model.c $(DISPLAY_UI)/src/app/extension_service.c $(UI_LIBS) -o $(UI_OUT)
	dbus-run-session -- $(UI_OUT)
