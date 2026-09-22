#define _POSIX_C_SOURCE 200809L
#include "extension_service.h"
#include "pilot/ui.h"
#include <assert.h>
#include <dbus/dbus.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static pilot_ui *ui;
static pilot_ui_page id, dialog;
static unsigned ready, actions, results, resets, errors;
static int defer_action;
static uint64_t deferred;
static eu_page snapshots[EU_PAGES];
static uint64_t generation;
static void callback(const pilot_ui_event *e, void *arg) {
    assert(arg == &ready);
    if (e->kind == PILOT_UI_READY)
        ready++;
    if (e->kind == PILOT_UI_ACTION) {
        actions++;
        if (defer_action)
            deferred = e->action_id;
        else
            assert(pilot_ui_complete(ui, e->page, e->action_id, true) == PILOT_OK);
    }
    if (e->kind == PILOT_UI_RESULT) {
        results++;
        assert(e->status == 0 || (defer_action && e->status == PILOT_TIMEOUT));
    }
    if (e->kind == PILOT_UI_RESET)
        resets++;
    if (e->kind == PILOT_UI_ERROR) {
        errors++;
        fprintf(stderr, "ui error %d\n", e->status);
    }
}
static void pause_ms(unsigned ms) {
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}
static void pump(unsigned ms) {
    for (unsigned t = 0; t < ms; t += 5) {
        int rc = pilot_ui_poll(ui);
        assert(rc == 0 || rc == PILOT_UI_BUSY);
        pause_ms(5);
    }
    extension_service_snapshot(snapshots, &generation);
}
static eu_page *lookup(pilot_ui_page local) {
    for (unsigned i = 0; i < EU_PAGES; i++)
        if (snapshots[i].handle && snapshots[i].local_id == local)
            return &snapshots[i];
    return NULL;
}
int main(void) {
    assert(dbus_threads_init_default());
    assert(extension_service_start() == 0);
    pause_ms(200);
    assert(pilot_ui_open(callback, &ready, &ui) == 0);
    pilot_ui_control controls[] = {
        {.id = 1,
         .kind = PILOT_UI_NUMBER,
         .label = "Temperature",
         .text = "C",
         .value = 20,
         .maximum = 1000,
         .step = 1,
         .enabled = true},
        {.id = 2, .kind = PILOT_UI_TOGGLE, .label = "Enabled", .maximum = 1, .enabled = true}};
    pilot_ui_page_options o = {
        .title = "Integration", .controls = controls, .count = 2, .update_hz = 10};
    assert(pilot_ui_register(ui, &o, &id) == 0);
    pump(500);
    assert(ready == 1 && lookup(id));
    for (unsigned i = 0; i < 1000; i++)
        assert(pilot_ui_set_value(ui, id, 1, i) == 0);
    pump(1300);
    assert(lookup(id)->controls[0].value == 999);
    assert(lookup(id)->revision <= 3); /* All 1000 writes collapse into one patch. */
    assert(pilot_ui_set_value(ui, id, 1, NAN) == PILOT_INVALID_ARGUMENT);
    assert(pilot_ui_set_text(ui, id, 1, "\xff") == PILOT_INVALID_ARGUMENT);
    assert(pilot_ui_begin(ui, id) == 0);
    assert(pilot_ui_set_value(ui, id, 1, 33) == 0);
    assert(pilot_ui_set_text(ui, id, 1, "batch") == 0);
    pump(200);
    assert(lookup(id)->controls[0].value == 999);
    assert(pilot_ui_commit(ui, id) == 0);
    pump(1500);
    assert(lookup(id)->controls[0].value == 33 && !strcmp(lookup(id)->controls[0].text, "batch"));
    assert(pilot_ui_begin(ui, id) == 0);
    assert(pilot_ui_set_value(ui, id, 1, 99) == 0);
    assert(pilot_ui_cancel(ui, id) == 0);
    uint32_t handle = lookup(id)->handle;
    extension_service_visible(handle);
    uint64_t before = lookup(id)->revision;
    for (unsigned i = 0; i < 500; i++) {
        assert(pilot_ui_set_value(ui, id, 1, i) == 0);
        pump(5);
    }
    pump(1200);
    assert(lookup(id)->controls[0].value == 499);
    assert(lookup(id)->revision - before <= 38);
    assert(extension_service_action(handle, 2, 1));
    pump(500);
    assert(actions == 1 && results == 1 && lookup(id)->controls[1].value == 1);
    defer_action = 1;
    assert(extension_service_action(handle, 2, 0));
    pump(5300);
    assert(deferred && !lookup(id)->pending && lookup(id)->controls[1].value == 1);
    unsigned previous_resets = resets;
    assert(pilot_ui_complete(ui, id, deferred, true) == 0);
    pump(300);
    assert(resets == previous_resets && lookup(id) && errors == 1);
    defer_action = 0;
    o.title = "Confirm";
    o.dialog_timeout_ms = 1500;
    assert(pilot_ui_register(ui, &o, &dialog) == 0);
    assert(pilot_ui_show(ui, dialog) == 0);
    pump(500);
    assert(ready == 2 && lookup(dialog)->shown);
    pump(1800);
    assert(!lookup(dialog)->shown);
    assert(pilot_ui_show(ui, dialog) == 0);
    pump(500);
    assert(lookup(dialog)->shown);
    /* An unresponsive owner loses pages/focus; resume restores state, not SHOW. */
    pause_ms(6500);
    pump(1500);
    assert(resets >= 1 && ready >= 4 && lookup(id) && lookup(dialog));
    assert(lookup(id)->controls[0].value == 499 && !lookup(dialog)->shown);
    assert(actions == 2);
    assert(pilot_ui_remove(ui, dialog) == 0);
    pump(500);
    assert(!lookup(dialog));
    pilot_ui_control many[32];
    for (unsigned i = 0; i < 32; i++)
        many[i] =
            (pilot_ui_control){.id = i + 1, .kind = PILOT_UI_TEXT, .label = "Text", .maximum = 100};
    o = (pilot_ui_page_options){.title = "Bounded batches", .controls = many, .count = 32};
    pilot_ui_page large;
    assert(pilot_ui_register(ui, &o, &large) == 0);
    pump(500);
    assert(lookup(large));
    char long_text[128];
    memset(long_text, 'a', 127);
    long_text[127] = 0;
    assert(pilot_ui_begin(ui, large) == 0);
    for (unsigned i = 1; i <= 32; i++)
        assert(pilot_ui_set_text(ui, large, i, long_text) == 0);
    assert(pilot_ui_commit(ui, large) == PILOT_UI_BUSY);
    assert(pilot_ui_cancel(ui, large) == 0);
    assert(pilot_ui_begin(ui, large) == 0);
    for (unsigned i = 1; i <= 32; i++) {
        assert(pilot_ui_set_text(ui, large, i, "short") == 0);
        assert(pilot_ui_set_value(ui, large, i, 10) == 0);
        assert(pilot_ui_set_enabled(ui, large, i, true) == 0);
    }
    assert(pilot_ui_commit(ui, large) == PILOT_UI_BUSY);
    assert(pilot_ui_cancel(ui, large) == 0);
    pump(200);
    assert(!lookup(large)->controls[0].text[0] && !lookup(large)->controls[0].enabled);
    pilot_ui_close(ui);
    ui = NULL;
    pause_ms(300);
    extension_service_snapshot(snapshots, &generation);
    for (unsigned i = 0; i < EU_PAGES; i++)
        assert(!snapshots[i].handle);
    printf("PASS actual service + SDK: coalescing, batch isolation, rate limits, actions, TTL, "
           "reconnect, owner cleanup; ready=%u errors=%u\n",
           ready, errors);
}
