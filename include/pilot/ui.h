#ifndef PILOT_UI_H
#define PILOT_UI_H
#include "pilot.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct pilot_ui pilot_ui;
typedef uint32_t pilot_ui_page;
typedef enum {
    PILOT_UI_TEXT = 1,
    PILOT_UI_NUMBER,
    PILOT_UI_PROGRESS,
    PILOT_UI_TOGGLE,
    PILOT_UI_BUTTON
} pilot_ui_kind;
typedef struct {
    uint32_t id;
    pilot_ui_kind kind;
    const char *label, *text;
    double value, minimum, maximum, step;
    bool enabled;
} pilot_ui_control;
typedef struct {
    const char *title;
    const pilot_ui_control *controls;
    size_t count;               /* 1..32; labels <=47, text <=127 UTF-8 bytes. */
    unsigned update_hz;         /* 1..20, zero selects 10. */
    unsigned dialog_timeout_ms; /* zero: menu page; 1000..30000: dialog. */
} pilot_ui_page_options;
typedef enum {
    PILOT_UI_READY = 1,
    PILOT_UI_ACTION,
    PILOT_UI_RESULT,
    PILOT_UI_CLOSED,
    PILOT_UI_RESET,
    PILOT_UI_ERROR
} pilot_ui_event_kind;
typedef struct {
    pilot_ui_event_kind kind;
    pilot_ui_page page;
    uint32_t control;
    uint64_t action_id;
    double value;
    int status;
} pilot_ui_event;
typedef void (*pilot_ui_callback)(const pilot_ui_event *event, void *userdata);

/* One instance per thread. Call poll at least every 100 ms. Callbacks may set
 * values or complete actions, but must not close or recursively poll. */
PILOT_API int pilot_ui_open(pilot_ui_callback callback, void *userdata, pilot_ui **out);
PILOT_API void pilot_ui_close(pilot_ui *ui);
/* Registration is asynchronous; READY confirms acceptance. At most four pages. */
PILOT_API int pilot_ui_register(pilot_ui *ui, const pilot_ui_page_options *options,
                                pilot_ui_page *out);
PILOT_API int pilot_ui_remove(pilot_ui *ui, pilot_ui_page page);
/* Dialogs only. Never steals focus for a normal menu page. Not replayed on reset. */
PILOT_API int pilot_ui_show(pilot_ui *ui, pilot_ui_page page);
PILOT_API int pilot_ui_hide(pilot_ui *ui, pilot_ui_page page);
PILOT_API int pilot_ui_begin(pilot_ui *ui, pilot_ui_page page);
/* Atomic batch <=64 changes/4 KiB. BUSY leaves the draft intact for reduction
 * or cancel; a batch is never split across multiple screen state revisions. */
PILOT_API int pilot_ui_commit(pilot_ui *ui, pilot_ui_page page);
PILOT_API int pilot_ui_cancel(pilot_ui *ui, pilot_ui_page page);
PILOT_API int pilot_ui_set_text(pilot_ui *ui, pilot_ui_page page, uint32_t control,
                                const char *text);
PILOT_API int pilot_ui_set_value(pilot_ui *ui, pilot_ui_page page, uint32_t control, double value);
PILOT_API int pilot_ui_set_enabled(pilot_ui *ui, pilot_ui_page page, uint32_t control,
                                   bool enabled);
/* Complete each ACTION once. Rejected/expired actions do not change the value.
 * A full command queue returns PILOT_UI_BUSY, never silently drops an action. */
#define PILOT_UI_BUSY PILOT_BUSY
PILOT_API int pilot_ui_complete(pilot_ui *ui, pilot_ui_page page, uint64_t action_id,
                                bool accepted);
/* Nonblocking I/O, bounded work, no frame/operation replay after reconnect. */
PILOT_API int pilot_ui_poll(pilot_ui *ui);

#ifdef __cplusplus
}
#endif
#endif
