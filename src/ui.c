#define _POSIX_C_SOURCE 200809L
#include "pilot/ui.h"
#include "private/runtime.h"
#include "private/ui_wire.h"
#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    uint32_t id, hz, ttl, count;
    char title[EU_TITLE];
    eu_control current[EU_CONTROLS], draft[EU_CONTROLS], confirmed[EU_CONTROLS];
    int registered, registration_sent, batching, removing;
    uint64_t revision, next_send, last_send;
} ui_page;
typedef struct {
    uint32_t op, page;
    uint64_t action;
    int accepted;
} ui_command;
struct pilot_ui {
    DBusConnection *bus;
    pilot_ui_callback callback;
    void *userdata;
    ui_page pages[4];
    ui_command commands[16], sending;
    unsigned head, count, round_robin;
    uint32_t next_page, serial;
    uint64_t epoch, deadline, heartbeat, retry_at;
    uint64_t budget_at;
    double bytes;
    eu_control sent[EU_CONTROLS];
    uint8_t sent_masks[EU_CONTROLS];
    char owner[64];
    int polling;
};
static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int status(unsigned code) {
    switch (code) {
    case EU_OK:
        return PILOT_OK;
    case EU_BUSY:
        return PILOT_UI_BUSY;
    case EU_INVALID:
        return PILOT_INVALID_ARGUMENT;
    case EU_DENIED:
        return PILOT_ACCESS_DENIED;
    case EU_STALE:
        return PILOT_TIMEOUT;
    case EU_MISSING:
        return PILOT_DISCONNECTED;
    default:
        return PILOT_BAD_REPLY;
    }
}
static ui_page *page(pilot_ui *ui, uint32_t id) {
    if (ui)
        for (unsigned i = 0; i < 4; i++)
            if (id && ui->pages[i].id == id)
                return &ui->pages[i];
    return NULL;
}
static void notify(pilot_ui *u, unsigned kind, uint32_t id, uint32_t control, uint64_t action,
                   double value, int code) {
    pilot_ui_event e = {(pilot_ui_event_kind)kind, id, control, action, value, code};
    if (u->callback)
        u->callback(&e, u->userdata);
}
static int connect_bus(pilot_ui *u) {
#ifdef PILOT_UI_TEST_SESSION
    u->bus = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);
#else
    u->bus = dbus_bus_get_private(DBUS_BUS_SYSTEM, NULL);
#endif
    if (!u->bus)
        return PILOT_DISCONNECTED;
    dbus_connection_set_exit_on_disconnect(u->bus, FALSE);
    dbus_connection_set_max_message_size(u->bus, EU_MAX + 2048);
    dbus_connection_set_max_received_size(u->bus, 65536);
    dbus_bus_add_match(u->bus,
                       "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop."
                       "DBus',member='NameOwnerChanged',arg0='tech.andless.Display'",
                       NULL);
    return PILOT_OK;
}
static void reset(pilot_ui *u) {
    u->serial = 0;
    u->epoch = 0;
    u->owner[0] = 0;
    u->count = 0;
    u->head = 0;
    u->heartbeat = 0;
    for (unsigned i = 0; i < 4; i++) {
        if (u->pages[i].removing)
            memset(&u->pages[i], 0, sizeof(ui_page));
        else {
            u->pages[i].registered = 0;
            u->pages[i].registration_sent = 0;
            u->pages[i].next_send = 0;
        }
    }
    notify(u, EU_RESET, 0, 0, 0, 0, PILOT_DISCONNECTED);
}
int pilot_ui_open(pilot_ui_callback cb, void *userdata, pilot_ui **out) {
    if (!out)
        return PILOT_INVALID_ARGUMENT;
    *out = NULL;
    if (!pilot_private_init_threads())
        return PILOT_NO_MEMORY;
    pilot_ui *u = calloc(1, sizeof(*u));
    if (!u)
        return PILOT_NO_MEMORY;
    u->callback = cb;
    u->userdata = userdata;
    u->bytes = 16384;
    u->budget_at = now_ms();
    int rc = connect_bus(u);
    if (rc) {
        free(u);
        return rc;
    }
    *out = u;
    return PILOT_OK;
}
void pilot_ui_close(pilot_ui *u) {
    if (!u || u->polling)
        return;
    if (u->bus) {
        dbus_connection_close(u->bus);
        dbus_connection_unref(u->bus);
    }
    free(u);
}
int pilot_ui_register(pilot_ui *u, const pilot_ui_page_options *o, pilot_ui_page *out) {
    if (!u || !o || !out || !o->controls || !o->count || o->count > EU_CONTROLS ||
        !eu_string_ok(o->title, EU_TITLE) || o->update_hz > 20 ||
        (o->dialog_timeout_ms && (o->dialog_timeout_ms < 1000 || o->dialog_timeout_ms > 30000)))
        return PILOT_INVALID_ARGUMENT;
    ui_page candidate = {0}, *p = NULL;
    for (unsigned i = 0; i < 4; i++)
        if (!u->pages[i].id) {
            p = &u->pages[i];
            break;
        }
    if (!p)
        return PILOT_UI_BUSY;
    candidate.count = (uint32_t)o->count;
    candidate.hz = o->update_hz ? o->update_hz : 10;
    candidate.ttl = o->dialog_timeout_ms;
    strcpy(candidate.title, o->title);
    for (unsigned i = 0; i < candidate.count; i++) {
        const pilot_ui_control *in = &o->controls[i];
        eu_control *c = &candidate.current[i];
        if (!eu_string_ok(in->label, EU_TITLE) || !eu_string_ok(in->text ? in->text : "", EU_TEXT))
            return PILOT_INVALID_ARGUMENT;
        c->id = in->id;
        c->kind = in->kind;
        strcpy(c->label, in->label);
        strcpy(c->text, in->text ? in->text : "");
        c->value = in->value;
        c->minimum = in->minimum;
        c->maximum = in->maximum;
        c->step = in->step;
        c->enabled = in->enabled;
        if (!eu_valid_control(c))
            return PILOT_INVALID_ARGUMENT;
        for (unsigned j = 0; j < i; j++)
            if (candidate.current[j].id == c->id)
                return PILOT_INVALID_ARGUMENT;
    }
    candidate.id = ++u->next_page;
    if (!candidate.id)
        candidate.id = ++u->next_page;
    *p = candidate;
    *out = p->id;
    return PILOT_OK;
}
static int command(pilot_ui *u, uint32_t id, unsigned op, uint64_t action, int accepted) {
    ui_page *p = page(u, id);
    if (!p || p->removing)
        return PILOT_INVALID_ARGUMENT;
    if (u->count == 16)
        return PILOT_UI_BUSY;
    if ((op == EU_SHOW || op == EU_HIDE) && !p->ttl)
        return PILOT_INVALID_ARGUMENT;
    u->commands[(u->head + u->count++) % 16] = (ui_command){op, id, action, accepted};
    return PILOT_OK;
}
int pilot_ui_show(pilot_ui *u, pilot_ui_page id) {
    return command(u, id, EU_SHOW, 0, 0);
}
int pilot_ui_hide(pilot_ui *u, pilot_ui_page id) {
    return command(u, id, EU_HIDE, 0, 0);
}
int pilot_ui_remove(pilot_ui *u, pilot_ui_page id) {
    int rc = command(u, id, EU_REMOVE, 0, 0);
    if (!rc)
        page(u, id)->removing = 1;
    return rc;
}
int pilot_ui_complete(pilot_ui *u, pilot_ui_page id, uint64_t action, bool accepted) {
    if (!action)
        return PILOT_INVALID_ARGUMENT;
    return command(u, id, EU_ACK, action, accepted);
}
int pilot_ui_begin(pilot_ui *u, pilot_ui_page id) {
    ui_page *p = page(u, id);
    if (!p || p->removing)
        return PILOT_INVALID_ARGUMENT;
    if (p->batching)
        return PILOT_UI_BUSY;
    memcpy(p->draft, p->current, sizeof(p->draft));
    p->batching = 1;
    return PILOT_OK;
}
static int patch_fits(pilot_ui *u, ui_page *p, const eu_control *values) {
    int flight = u->serial && u->sending.page == p->id &&
                 (u->sending.op == EU_REGISTER || u->sending.op == EU_PATCH);
    if (!p->registered && !p->registration_sent)
        return 1;
    size_t bytes = 32;
    unsigned count = 0;
    for (unsigned i = 0; i < p->count; i++) {
        const eu_control *c = &values[i], *a = &p->confirmed[i];
        const eu_control *b = flight ? &u->sent[i] : a;
        if (strcmp(c->text, a->text) || strcmp(c->text, b->text)) {
            bytes += 12 + strlen(c->text);
            count++;
        }
        if (c->value != a->value || c->value != b->value) {
            bytes += 16;
            count++;
        }
        if (c->enabled != a->enabled || c->enabled != b->enabled) {
            bytes += 12;
            count++;
        }
    }
    return bytes <= 4096 && count <= 64;
}
int pilot_ui_commit(pilot_ui *u, pilot_ui_page id) {
    ui_page *p = page(u, id);
    if (!p || !p->batching || p->removing)
        return PILOT_INVALID_ARGUMENT;
    if (!patch_fits(u, p, p->draft))
        return PILOT_UI_BUSY;
    memcpy(p->current, p->draft, sizeof(p->current));
    p->batching = 0;
    return PILOT_OK;
}
int pilot_ui_cancel(pilot_ui *u, pilot_ui_page id) {
    ui_page *p = page(u, id);
    if (!p || !p->batching)
        return PILOT_INVALID_ARGUMENT;
    p->batching = 0;
    return PILOT_OK;
}
static eu_control *control(pilot_ui *u, uint32_t id, uint32_t cid) {
    ui_page *p = page(u, id);
    if (!p || p->removing)
        return NULL;
    for (unsigned i = 0; i < p->count; i++)
        if (p->current[i].id == cid)
            return p->batching ? &p->draft[i] : &p->current[i];
    return NULL;
}
int pilot_ui_set_text(pilot_ui *u, pilot_ui_page id, uint32_t cid, const char *text) {
    eu_control *c = control(u, id, cid);
    if (!c || !eu_string_ok(text, EU_TEXT))
        return PILOT_INVALID_ARGUMENT;
    char old[EU_TEXT];
    strcpy(old, c->text);
    strcpy(c->text, text);
    ui_page *p = page(u, id);
    if (!p->batching && !patch_fits(u, p, p->current)) {
        strcpy(c->text, old);
        return PILOT_UI_BUSY;
    }
    return PILOT_OK;
}
int pilot_ui_set_value(pilot_ui *u, pilot_ui_page id, uint32_t cid, double value) {
    eu_control *c = control(u, id, cid);
    if (!c)
        return PILOT_INVALID_ARGUMENT;
    eu_control test = *c;
    test.value = value;
    if (!eu_valid_control(&test))
        return PILOT_INVALID_ARGUMENT;
    double old = c->value;
    c->value = value;
    ui_page *p = page(u, id);
    if (!p->batching && !patch_fits(u, p, p->current)) {
        c->value = old;
        return PILOT_UI_BUSY;
    }
    return PILOT_OK;
}
int pilot_ui_set_enabled(pilot_ui *u, pilot_ui_page id, uint32_t cid, bool enabled) {
    eu_control *c = control(u, id, cid);
    if (!c)
        return PILOT_INVALID_ARGUMENT;
    uint8_t old = c->enabled;
    c->enabled = enabled;
    ui_page *p = page(u, id);
    if (!p->batching && !patch_fits(u, p, p->current)) {
        c->enabled = old;
        return PILOT_UI_BUSY;
    }
    return PILOT_OK;
}
static int get_wire(DBusMessage *msg, eu_wire *w) {
    if (!dbus_message_has_signature(msg, "ay"))
        return 0;
    DBusMessageIter it, arr;
    const uint8_t *data = NULL;
    int n = 0;
    dbus_message_iter_init(msg, &it);
    dbus_message_iter_recurse(&it, &arr);
    dbus_message_iter_get_fixed_array(&arr, &data, &n);
    if (n < 0 || n > EU_MAX)
        return 0;
    memset(w, 0, sizeof(*w));
    if (n)
        memcpy(w->data, data, n);
    w->size = (size_t)n;
    return 1;
}
static void receive(pilot_ui *u, DBusMessage *msg, uint64_t now) {
    if (dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameOwnerChanged") &&
        dbus_message_has_sender(msg, DBUS_SERVICE_DBUS)) {
        const char *name, *old, *next;
        if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &old,
                                  DBUS_TYPE_STRING, &next, DBUS_TYPE_INVALID) &&
            !strcmp(name, EU_NAME) && old[0] && u->owner[0] && !strcmp(old, u->owner))
            reset(u);
        return;
    }
    eu_wire w;
    if (dbus_message_is_signal(msg, EU_IFACE, "Event") && dbus_message_has_path(msg, EU_PATH) &&
        u->owner[0] && dbus_message_has_sender(msg, u->owner) && get_wire(msg, &w)) {
        uint64_t epoch = eu_r64(&w);
        uint32_t id = eu_r32(&w), kind = eu_r32(&w);
        uint64_t action = eu_r64(&w);
        uint32_t cid = eu_r32(&w);
        double value = eu_rd(&w);
        unsigned code = eu_r32(&w);
        ui_page *p = page(u, id);
        if (w.bad || w.pos != w.size || epoch != u->epoch || !p || !p->registered ||
            kind < EU_ACTION || kind > EU_CLOSED)
            return;
        if (kind == EU_RESULT && code == EU_OK)
            for (unsigned i = 0; i < p->count; i++)
                if (p->current[i].id == cid) {
                    /* A newer app value wins; a local draft never gets overwritten. */
                    if (p->current[i].value == p->confirmed[i].value)
                        p->current[i].value = value;
                    p->confirmed[i].value = value;
                }
        notify(u, kind, id, cid, action, value, status(code));
        return;
    }
    if (!u->serial || dbus_message_get_reply_serial(msg) != u->serial)
        return;
    if (u->owner[0] && !dbus_message_has_sender(msg, u->owner) &&
        !dbus_message_has_sender(msg, DBUS_SERVICE_DBUS))
        return;
    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_RETURN &&
        dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_ERROR)
        return;
    ui_command sending = u->sending;
    u->serial = 0;
    ui_page *p = page(u, sending.page);
    if (!get_wire(msg, &w)) {
        if (p && sending.op == EU_REMOVE)
            p->removing = 0;
        u->retry_at = now + 1000;
        notify(u, EU_ERROR, sending.page, 0, sending.action, 0, PILOT_REMOTE_ERROR);
        return;
    }
    unsigned code = eu_r32(&w);
    uint64_t epoch = eu_r64(&w);
    (void)eu_r32(&w);
    unsigned hz = eu_r32(&w);
    if (w.bad || w.pos != w.size) {
        notify(u, EU_ERROR, sending.page, 0, sending.action, 0, PILOT_BAD_REPLY);
        return;
    }
    if (code == EU_MISSING || (u->epoch && u->epoch != epoch)) {
        reset(u);
        return;
    }
    if (code != EU_OK) {
        if (p && sending.op == EU_REMOVE)
            p->removing = 0;
        if (p && (sending.op == EU_PATCH || sending.op == EU_REGISTER))
            p->next_send =
                now + (code == EU_BUSY ? (hz && hz <= 20 ? (1000 + hz - 1) / hz : 1000) : 5000);
        if (code != EU_BUSY || (sending.op != EU_PATCH && sending.op != EU_REGISTER))
            notify(u, EU_ERROR, sending.page, 0, sending.action, 0, status(code));
        return;
    }
    const char *sender = dbus_message_get_sender(msg);
    if (!sender || strlen(sender) >= sizeof(u->owner))
        return;
    if (!u->epoch) {
        u->epoch = epoch;
        strcpy(u->owner, sender);
    }
    if (sending.op == EU_REGISTER && p) {
        p->registered = 1;
        memcpy(p->confirmed, u->sent, sizeof(p->confirmed));
        notify(u, EU_READY, p->id, 0, 0, 0, PILOT_OK);
    } else if (sending.op == EU_PATCH && p) {
        if (hz && hz <= 20)
            p->next_send = p->last_send + (1000 + hz - 1) / hz;
        for (unsigned i = 0; i < p->count; i++) {
            if (u->sent_masks[i] & 1)
                strcpy(p->confirmed[i].text, u->sent[i].text);
            if (u->sent_masks[i] & 2)
                p->confirmed[i].value = u->sent[i].value;
            if (u->sent_masks[i] & 4)
                p->confirmed[i].enabled = u->sent[i].enabled;
        }
    } else if (sending.op == EU_REMOVE && p)
        memset(p, 0, sizeof(*p));
}
static int send(pilot_ui *u, eu_wire *w, ui_command cmd, uint64_t now) {
    if (w->bad)
        return PILOT_INVALID_ARGUMENT;
    u->bytes = fmin(16384, u->bytes + (now - u->budget_at) * 16.384);
    u->budget_at = now;
    if (u->bytes < w->size + 128)
        return PILOT_UI_BUSY;
    DBusMessage *m = dbus_message_new_method_call(EU_NAME, EU_PATH, EU_IFACE, "Call");
    if (!m)
        return PILOT_NO_MEMORY;
    const uint8_t *data = w->data;
    dbus_uint32_t serial = 0;
    int ok = dbus_message_append_args(m, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE, &data, (int)w->size,
                                      DBUS_TYPE_INVALID) &&
             dbus_connection_send(u->bus, m, &serial);
    dbus_message_unref(m);
    if (!ok)
        return PILOT_NO_MEMORY;
    u->bytes -= w->size + 128;
    u->serial = serial;
    u->sending = cmd;
    u->deadline = now + 1500;
    return PILOT_OK;
}
static int schedule(pilot_ui *u, uint64_t now) {
    eu_wire w;
    if (u->epoch && now >= u->heartbeat) {
        eu_header(&w, EU_PING, u->epoch, 0, 0);
        u->heartbeat = now + 1000;
        return send(u, &w, (ui_command){.op = EU_PING}, now);
    }
    /* Register before dependent commands; one call in flight for all pages. */
    for (unsigned i = 0; i < 4; i++) {
        ui_page *p = &u->pages[i];
        if (!p->id || p->registered || now < p->next_send)
            continue;
        if (!p->registration_sent) {
            p->revision++;
            memcpy(p->confirmed, p->current, sizeof(p->confirmed));
            p->registration_sent = 1;
        }
        eu_header(&w, EU_REGISTER, 0, p->id, p->revision);
        eu_str(&w, p->title);
        eu_u32(&w, p->hz);
        eu_u32(&w, p->ttl);
        eu_u32(&w, p->count);
        for (unsigned j = 0; j < p->count; j++)
            eu_put_control(&w, &p->confirmed[j]);
        memcpy(u->sent, p->confirmed, sizeof(u->sent));
        return send(u, &w, (ui_command){.op = EU_REGISTER, .page = p->id}, now);
    }
    if (u->count) {
        ui_command cmd = u->commands[u->head];
        ui_page *p = page(u, cmd.page);
        if (!p) {
            u->head = (u->head + 1) % 16;
            u->count--;
            return 0;
        }
        if (p->registered) {
            eu_header(&w, cmd.op, u->epoch, cmd.page, 0);
            if (cmd.op == EU_ACK) {
                eu_u64(&w, cmd.action);
                eu_u32(&w, cmd.accepted);
            }
            int rc = send(u, &w, cmd, now);
            if (!rc) {
                u->head = (u->head + 1) % 16;
                u->count--;
            }
            return rc;
        }
    }
    for (unsigned n = 0; n < 4; n++) {
        unsigned i = (u->round_robin + n) % 4;
        ui_page *p = &u->pages[i];
        if (!p->id || !p->registered || p->removing || now < p->next_send)
            continue;
        eu_header(&w, EU_PATCH, u->epoch, p->id, p->revision + 1);
        size_t count_at = w.size;
        eu_u32(&w, 0);
        unsigned changes = 0;
        memset(u->sent_masks, 0, sizeof(u->sent_masks));
        for (unsigned j = 0; j < p->count; j++) {
            eu_control *c = &p->current[j], *ack = &p->confirmed[j];
            for (unsigned prop = 1; prop <= 3; prop++) {
                int different = prop == 1   ? strcmp(c->text, ack->text)
                                : prop == 2 ? (c->value != ack->value)
                                            : (c->enabled != ack->enabled);
                size_t bytes = prop == 1 ? 12 + strlen(c->text) : prop == 2 ? 16 : 12;
                if (!different)
                    continue;
                if (changes == 64 || w.size + bytes > 4096)
                    return PILOT_UI_BUSY;
                eu_u32(&w, c->id);
                eu_u32(&w, prop);
                if (prop == 1)
                    eu_str(&w, c->text);
                else if (prop == 2)
                    eu_double(&w, c->value);
                else
                    eu_u32(&w, c->enabled);
                u->sent_masks[j] |= (uint8_t)(1u << (prop - 1));
                changes++;
            }
        }
        if (!changes)
            continue;
        for (unsigned j = 0; j < 4; j++)
            w.data[count_at + j] = (uint8_t)(changes >> (8 * j));
        memcpy(u->sent, p->current, sizeof(u->sent));
        p->revision++;
        p->last_send = now;
        p->next_send = now + (1000 + p->hz - 1) / p->hz;
        u->round_robin = (i + 1) % 4;
        return send(u, &w, (ui_command){.op = EU_PATCH, .page = p->id}, now);
    }
    return PILOT_OK;
}
int pilot_ui_poll(pilot_ui *u) {
    if (!u || u->polling)
        return PILOT_INVALID_ARGUMENT;
    u->polling = 1;
    uint64_t now = now_ms();
    int rc = PILOT_OK;
    if (!u->bus || !dbus_connection_get_is_connected(u->bus)) {
        if (u->bus) {
            dbus_connection_close(u->bus);
            dbus_connection_unref(u->bus);
            u->bus = NULL;
            reset(u);
        }
        if (now < u->retry_at) {
            rc = PILOT_DISCONNECTED;
            goto done;
        }
        u->retry_at = now + 1000;
        rc = connect_bus(u);
        if (rc)
            goto done;
    }
    dbus_connection_read_write(u->bus, 0);
    for (unsigned i = 0; i < 32; i++) {
        DBusMessage *m = dbus_connection_pop_message(u->bus);
        if (!m)
            break;
        receive(u, m, now);
        dbus_message_unref(m);
    }
    if (u->serial && now >= u->deadline) {
        ui_command cmd = u->sending;
        u->serial = 0;
        u->retry_at = now + 1000;
        ui_page *p = page(u, cmd.page);
        if (p && cmd.op == EU_REMOVE)
            p->removing = 0;
        notify(u, EU_ERROR, cmd.page, 0, cmd.action, 0, PILOT_TIMEOUT);
        /* State/definitions may retry. Operations are never automatically replayed. */
    }
    if (!u->serial && now >= u->retry_at)
        rc = schedule(u, now);
done:
    u->polling = 0;
    return rc;
}
