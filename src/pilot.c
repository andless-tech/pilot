#include "private/transport.h"
#include "private/runtime.h"
#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VALUES 32768U
#define MAX_MESSAGE_BYTES (4 * 1024 * 1024)

typedef struct { pilot_signal_callback callback; void *userdata; bool owned; } subscription;
struct pilot_client {
    DBusConnection *rpc, *events;
    pilot_options options;
    subscription *subscriptions;
    char (*owners)[256];
    bool polling;
    bool session_bus;
};
struct pilot_reply {
    DBusMessage *message;
    pilot_value root;
};

static void clear_error(pilot_error *e) { if (e) memset(e, 0, sizeof(*e)); }
static int fail(pilot_error *e, int code, const char *name, const char *message)
{
    (void)name;
    if (e) {
        e->code = code;
        snprintf(e->message, sizeof(e->message), "%s", message ? message : pilot_status_string(code));
    }
    return code;
}
static int bus_error(pilot_error *e, DBusError *error)
{
    int code = PILOT_REMOTE_ERROR;
    if (dbus_error_has_name(error, DBUS_ERROR_NO_REPLY) || dbus_error_has_name(error, DBUS_ERROR_TIMEOUT)) code = PILOT_TIMEOUT;
    else if (dbus_error_has_name(error, DBUS_ERROR_DISCONNECTED) || dbus_error_has_name(error, DBUS_ERROR_NO_SERVER)) code = PILOT_DISCONNECTED;
    else if (dbus_error_has_name(error, DBUS_ERROR_ACCESS_DENIED)) code = PILOT_ACCESS_DENIED;
    else if (dbus_error_has_name(error, DBUS_ERROR_INVALID_ARGS)) code = PILOT_INVALID_ARGUMENT;
    else if (dbus_error_has_name(error, DBUS_ERROR_UNKNOWN_METHOD) || dbus_error_has_name(error, DBUS_ERROR_SERVICE_UNKNOWN) ||
             dbus_error_has_name(error, DBUS_ERROR_NAME_HAS_NO_OWNER) || dbus_error_has_name(error, DBUS_ERROR_UNKNOWN_INTERFACE)) code = PILOT_NOT_SUPPORTED;
    else if (dbus_error_has_name(error, DBUS_ERROR_NO_MEMORY)) code = PILOT_NO_MEMORY;
    int result = fail(e, code, NULL, NULL);
    dbus_error_free(error);
    return result;
}

static void free_value(pilot_value *v)
{
    pilot_value *items = (pilot_value *)v->items;
    for (size_t i = 0; items && i < v->count; ++i) free_value(&items[i]);
    free(items);
}
void pilot_reply_free(pilot_reply *r)
{
    if (!r) return;
    free_value(&r->root);
    dbus_message_unref(r->message);
    free(r);
}
size_t pilot_reply_count(const pilot_reply *r) { return r ? r->root.count : 0; }
const pilot_value *pilot_reply_at(const pilot_reply *r, size_t i) { return r ? pilot_value_at(&r->root, i) : NULL; }
const pilot_value *pilot_value_at(const pilot_value *v, size_t i) { return v && v->items && i < v->count ? v->items + i : NULL; }
const pilot_value *pilot_value_unwrap(const pilot_value *v)
{
    while (v && v->type == 'v') v = pilot_value_at(v, 0);
    return v;
}
const pilot_value *pilot_dict_get(const pilot_value *dict, const char *key)
{
    if (!dict || !key || dict->type != 'a') return NULL;
    for (size_t i = 0; i < dict->count; ++i) {
        const pilot_value *entry = pilot_value_at(dict, i);
        const pilot_value *k = pilot_value_at(entry, 0), *v = pilot_value_at(entry, 1);
        if (entry && entry->type == 'e' && k && k->type == 's' && !strcmp(k->as.string, key))
            return pilot_value_unwrap(v);
    }
    return NULL;
}

static int decode_values(DBusMessageIter *iter, pilot_value *parent, unsigned depth, size_t *budget);
static int decode_value(DBusMessageIter *iter, pilot_value *v, unsigned depth, size_t *budget)
{
    int type = dbus_message_iter_get_arg_type(iter);
    v->type = (char)type;
    switch (type) {
    case DBUS_TYPE_BOOLEAN: { dbus_bool_t b; dbus_message_iter_get_basic(iter, &b); v->as.boolean = b != 0; break; }
    case DBUS_TYPE_BYTE: dbus_message_iter_get_basic(iter, &v->as.byte); break;
    case DBUS_TYPE_INT16: dbus_message_iter_get_basic(iter, &v->as.i16); break;
    case DBUS_TYPE_UINT16: dbus_message_iter_get_basic(iter, &v->as.u16); break;
    case DBUS_TYPE_INT32: dbus_message_iter_get_basic(iter, &v->as.i32); break;
    case DBUS_TYPE_UINT32: dbus_message_iter_get_basic(iter, &v->as.u32); break;
    case DBUS_TYPE_INT64: dbus_message_iter_get_basic(iter, &v->as.i64); break;
    case DBUS_TYPE_UINT64: dbus_message_iter_get_basic(iter, &v->as.u64); break;
    case DBUS_TYPE_DOUBLE: dbus_message_iter_get_basic(iter, &v->as.real); break;
    case DBUS_TYPE_STRING: case DBUS_TYPE_OBJECT_PATH: case DBUS_TYPE_SIGNATURE:
        dbus_message_iter_get_basic(iter, &v->as.string); break;
    case DBUS_TYPE_ARRAY: case DBUS_TYPE_STRUCT: case DBUS_TYPE_DICT_ENTRY: case DBUS_TYPE_VARIANT: {
        if (depth >= 16) return PILOT_BAD_REPLY;
        DBusMessageIter child;
        dbus_message_iter_recurse(iter, &child);
        if (type == DBUS_TYPE_ARRAY && dbus_message_iter_get_element_type(iter) == DBUS_TYPE_BYTE) {
            int count = 0;
            dbus_message_iter_get_fixed_array(&child, &v->bytes, &count);
            if (count < 0 || count > MAX_MESSAGE_BYTES) return PILOT_BAD_REPLY;
            v->count = (size_t)count;
            return PILOT_OK;
        }
        return decode_values(&child, v, depth + 1, budget);
    }
    default: return PILOT_BAD_REPLY;
    }
    return PILOT_OK;
}
static int decode_values(DBusMessageIter *iter, pilot_value *parent, unsigned depth, size_t *budget)
{
    DBusMessageIter scan = *iter;
    size_t count = 0;
    while (dbus_message_iter_get_arg_type(&scan) != DBUS_TYPE_INVALID) {
        if (++count > *budget) return PILOT_BAD_REPLY;
        dbus_message_iter_next(&scan);
    }
    if (!count) return PILOT_OK;
    *budget -= count;
    pilot_value *items = calloc(count, sizeof(*items));
    if (!items) return PILOT_NO_MEMORY;
    parent->items = items;
    parent->count = count;
    for (size_t i = 0; i < count; ++i) {
        int rc = decode_value(iter, &items[i], depth, budget);
        if (rc != PILOT_OK) return rc;
        dbus_message_iter_next(iter);
    }
    return PILOT_OK;
}
static int decode_reply(DBusMessage *message, pilot_reply **out, pilot_error *e)
{
    pilot_reply *r = calloc(1, sizeof(*r));
    if (!r) return fail(e, PILOT_NO_MEMORY, NULL, NULL);
    r->message = dbus_message_ref(message);
    r->root.type = 'r';
    DBusMessageIter iter;
    size_t budget = MAX_VALUES;
    int rc = PILOT_OK;
    if (dbus_message_iter_init(message, &iter)) rc = decode_values(&iter, &r->root, 0, &budget);
    if (rc != PILOT_OK) { pilot_reply_free(r); return fail(e, rc, NULL, "Invalid or oversized D-Bus reply"); }
    *out = r;
    return PILOT_OK;
}

static void close_bus(DBusConnection **bus)
{
    if (*bus) { dbus_connection_close(*bus); dbus_connection_unref(*bus); *bus = NULL; }
}
static bool connected(const pilot_client *c)
{
    return c && c->rpc && c->events && dbus_connection_get_is_connected(c->rpc) && dbus_connection_get_is_connected(c->events);
}
static int match(pilot_client *c, const char *rule, bool add, pilot_error *e)
{
    DBusError error = DBUS_ERROR_INIT;
    if (add) dbus_bus_add_match(c->events, rule, &error);
    else dbus_bus_remove_match(c->events, rule, &error);
    if (dbus_error_is_set(&error)) return bus_error(e, &error);
    return PILOT_OK;
}
static int signal_match(pilot_client *c, unsigned id, bool add, pilot_error *e)
{
    const pilot_signal *s = &pilot_signals[id];
    const pilot_service *svc = &pilot_services[s->service];
    char rule[768];
    snprintf(rule, sizeof(rule), "type='signal',sender='%s',path='%s',interface='%s',member='%s'",
             svc->name, svc->path, svc->interface, s->name);
    return match(c, rule, add, e);
}
static int query_owner(pilot_client *c, unsigned id, pilot_error *e)
{
    DBusMessage *msg = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "GetNameOwner");
    if (!msg) return fail(e, PILOT_NO_MEMORY, NULL, NULL);
    const char *name = pilot_services[id].name;
    if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID)) {
        dbus_message_unref(msg); return fail(e, PILOT_NO_MEMORY, NULL, NULL);
    }
    DBusError error = DBUS_ERROR_INIT;
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(c->rpc, msg, c->options.timeout_ms, &error);
    dbus_message_unref(msg);
    if (!reply) {
        if (dbus_error_has_name(&error, DBUS_ERROR_NAME_HAS_NO_OWNER)) {
            c->owners[id][0] = 0; dbus_error_free(&error); return PILOT_OK;
        }
        return bus_error(e, &error);
    }
    const char *owner = NULL;
    bool valid = dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &owner, DBUS_TYPE_INVALID);
    if (valid) snprintf(c->owners[id], 256, "%s", owner);
    dbus_message_unref(reply);
    return valid ? PILOT_OK : fail(e, PILOT_BAD_REPLY, NULL, "Invalid GetNameOwner response");
}
static int connect_buses(pilot_client *c, pilot_error *e)
{
    DBusError error = DBUS_ERROR_INIT;
    c->rpc = dbus_bus_get_private(c->session_bus ? DBUS_BUS_SESSION : DBUS_BUS_SYSTEM, &error);
    if (!c->rpc) return bus_error(e, &error);
    dbus_connection_set_exit_on_disconnect(c->rpc, FALSE);
    dbus_connection_set_max_message_size(c->rpc, MAX_MESSAGE_BYTES);
    dbus_connection_set_max_received_size(c->rpc, MAX_MESSAGE_BYTES);
    c->events = dbus_bus_get_private(c->session_bus ? DBUS_BUS_SESSION : DBUS_BUS_SYSTEM, &error);
    if (!c->events) { close_bus(&c->rpc); return bus_error(e, &error); }
    dbus_connection_set_exit_on_disconnect(c->events, FALSE);
    dbus_connection_set_max_message_size(c->events, MAX_MESSAGE_BYTES);
    dbus_connection_set_max_received_size(c->events, MAX_MESSAGE_BYTES);
    int rc = PILOT_OK;
    for (size_t i = 0; i < pilot_service_count; ++i) {
        char rule[512];
        snprintf(rule, sizeof(rule), "type='signal',sender='org.freedesktop.DBus',path='/org/freedesktop/DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0='%s'", pilot_services[i].name);
        if ((rc = match(c, rule, true, e)) != PILOT_OK || (rc = query_owner(c, i, e)) != PILOT_OK) goto failed;
    }
    for (size_t i = 0; i < pilot_signal_count; ++i)
        if (c->subscriptions[i].callback && (rc = signal_match(c, i, true, e)) != PILOT_OK) goto failed;
    return PILOT_OK;
failed:
    close_bus(&c->events); close_bus(&c->rpc); return rc;
}
int pilot_open_internal(const pilot_options *options, bool session_bus, pilot_client **out, pilot_error *e)
{
    clear_error(e);
    if (!out) return fail(e, PILOT_INVALID_ARGUMENT, NULL, NULL);
    *out = NULL;
    pilot_options opts = {0};
    if (options) opts = *options;
    if (!opts.timeout_ms) opts.timeout_ms = PILOT_DEFAULT_TIMEOUT_MS;
    if (opts.timeout_ms < 1 || opts.timeout_ms > 60000) return fail(e, PILOT_INVALID_ARGUMENT, NULL, "Invalid timeout");
    if (!pilot_private_init_threads()) return fail(e, PILOT_NO_MEMORY, NULL, "D-Bus thread initialization failed");
    pilot_client *c = calloc(1, sizeof(*c));
    if (!c) return fail(e, PILOT_NO_MEMORY, NULL, NULL);
    c->options = opts;
    c->session_bus = session_bus;
    c->subscriptions = calloc(pilot_signal_count, sizeof(*c->subscriptions));
    c->owners = calloc(pilot_service_count, sizeof(*c->owners));
    if (!c->subscriptions || !c->owners) { pilot_close(c); return fail(e, PILOT_NO_MEMORY, NULL, NULL); }
    int rc = connect_buses(c, e);
    if (rc != PILOT_OK) { pilot_close(c); return rc; }
    *out = c;
    return PILOT_OK;
}
int pilot_open(const pilot_options *options, pilot_client **out, pilot_error *e)
{
    return pilot_open_internal(options, false, out, e);
}
void pilot_close(pilot_client *c)
{
    if (!c || c->polling) return;
    close_bus(&c->rpc); close_bus(&c->events);
    for (size_t i = 0; c->subscriptions && i < pilot_signal_count; ++i)
        if (c->subscriptions[i].owned) free(c->subscriptions[i].userdata);
    free(c->subscriptions); free(c->owners); free(c);
}
int pilot_reconnect(pilot_client *c, pilot_error *e)
{
    clear_error(e);
    if (!c || c->polling) return fail(e, PILOT_INVALID_ARGUMENT, NULL, NULL);
    close_bus(&c->rpc); close_bus(&c->events);
    memset(c->owners, 0, pilot_service_count * sizeof(*c->owners));
    return connect_buses(c, e);
}
int pilot_service_available(pilot_client *c, unsigned service, bool *available, pilot_error *e)
{
    clear_error(e);
    if (available) *available = false;
    if (!c || !available || service >= pilot_service_count) return fail(e, PILOT_INVALID_ARGUMENT, NULL, NULL);
    if (!connected(c)) return fail(e, PILOT_DISCONNECTED, NULL, NULL);
    int rc = query_owner(c, service, e);
    if (rc == PILOT_OK) *available = c->owners[service][0] != 0;
    return rc;
}

static int append_arg(DBusMessageIter *iter, const pilot_value *v)
{
    const void *ptr = &v->as;
    dbus_bool_t boolean = FALSE;
    if (v->type == 'b') { boolean = v->as.boolean ? TRUE : FALSE; ptr = &boolean; }
    if (v->type == 's' && (!v->as.string || strlen(v->as.string) > 65536 || !dbus_validate_utf8(v->as.string, NULL)))
        return PILOT_INVALID_ARGUMENT;
    return dbus_message_iter_append_basic(iter, v->type, ptr) ? PILOT_OK : PILOT_NO_MEMORY;
}
static int call_method(pilot_client *c, const pilot_method *m, const pilot_value *args,
                       size_t count, pilot_reply **out, pilot_error *e)
{
    if (!out) return fail(e, PILOT_INVALID_ARGUMENT, NULL, NULL);
    *out = NULL;
    if (!c || (count && !args) || count != strlen(m->input_signature)) return fail(e, PILOT_INVALID_ARGUMENT, NULL, NULL);
    for (size_t i = 0; i < count; ++i)
        if (args[i].type != m->input_signature[i]) return fail(e, PILOT_INVALID_ARGUMENT, NULL, "Input signature mismatch");
    if (!connected(c)) return fail(e, PILOT_DISCONNECTED, NULL, NULL);
    const pilot_service *s = &pilot_services[m->service];
    const char *interface = !strcmp(m->name, "Introspect") ? DBUS_INTERFACE_INTROSPECTABLE : s->interface;
    DBusMessage *msg = dbus_message_new_method_call(s->name, s->path, interface, m->name);
    if (!msg) return fail(e, PILOT_NO_MEMORY, NULL, NULL);
    dbus_message_set_auto_start(msg, FALSE);
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg, &iter);
    for (size_t i = 0; i < count; ++i) {
        int rc = append_arg(&iter, &args[i]);
        if (rc != PILOT_OK) { dbus_message_unref(msg); return fail(e, rc, NULL, "Invalid method argument"); }
    }
    DBusError error = DBUS_ERROR_INIT;
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(c->rpc, msg, c->options.timeout_ms, &error);
    dbus_message_unref(msg);
    if (!reply) return bus_error(e, &error);
    int rc;
    if (!dbus_message_has_signature(reply, m->output_signature))
        rc = fail(e, PILOT_BAD_REPLY, NULL, "Reply signature does not match the SDK ABI");
    else rc = decode_reply(reply, out, e);
    dbus_message_unref(reply);
    return rc;
}
int pilot_call(pilot_client *c, unsigned method, const pilot_value *args, size_t count, pilot_reply **out, pilot_error *e)
{
    clear_error(e);
    if (out) *out = NULL;
    if (method >= pilot_method_count) return fail(e, PILOT_INVALID_ARGUMENT, NULL, NULL);
    return call_method(c, &pilot_methods[method], args, count, out, e);
}
int pilot_introspect(pilot_client *c, unsigned service, pilot_reply **out, pilot_error *e)
{
    clear_error(e);
    if (out) *out = NULL;
    if (service >= pilot_service_count) return fail(e, PILOT_INVALID_ARGUMENT, NULL, NULL);
    pilot_method method = {service, "Introspect", "", "s"};
    return call_method(c, &method, NULL, 0, out, e);
}
int pilot_subscribe(pilot_client *c, unsigned id, pilot_signal_callback callback, void *userdata, pilot_error *e)
{
    clear_error(e);
    if (!c || id >= pilot_signal_count || !callback) return fail(e, PILOT_INVALID_ARGUMENT, NULL, NULL);
    if (!connected(c)) return fail(e, PILOT_DISCONNECTED, NULL, NULL);
    if (!c->subscriptions[id].callback) {
        int rc = signal_match(c, id, true, e);
        if (rc != PILOT_OK) return rc;
    }
    if (c->subscriptions[id].owned) free(c->subscriptions[id].userdata);
    c->subscriptions[id] = (subscription){callback, userdata, false};
    return PILOT_OK;
}
int pilot_watch_install(pilot_client *c, unsigned id, pilot_signal_callback callback, void *context, pilot_error *e)
{
    if (!callback) { free(context); return pilot_unsubscribe(c, id, e); }
    int rc = pilot_subscribe(c, id, callback, context, e);
    if (rc == PILOT_OK) c->subscriptions[id].owned = true;
    else free(context);
    return rc;
}
int pilot_unsubscribe(pilot_client *c, unsigned id, pilot_error *e)
{
    clear_error(e);
    if (!c || id >= pilot_signal_count) return fail(e, PILOT_INVALID_ARGUMENT, NULL, NULL);
    if (!c->subscriptions[id].callback) return PILOT_OK;
    if (connected(c)) {
        int rc = signal_match(c, id, false, e);
        if (rc != PILOT_OK) return rc;
    }
    if (c->subscriptions[id].owned) free(c->subscriptions[id].userdata);
    c->subscriptions[id] = (subscription){0};
    return PILOT_OK;
}
static int dispatch_signal(pilot_client *c, DBusMessage *msg, pilot_error *e)
{
    const char *sender = dbus_message_get_sender(msg);
    if (!sender) return 0;
    if (!strcmp(sender, DBUS_SERVICE_DBUS) && dbus_message_has_path(msg, DBUS_PATH_DBUS) &&
        dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {
        const char *name, *old_owner, *new_owner;
        if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &old_owner,
                                 DBUS_TYPE_STRING, &new_owner, DBUS_TYPE_INVALID))
            for (size_t i = 0; i < pilot_service_count; ++i)
                if (!strcmp(name, pilot_services[i].name)) snprintf(c->owners[i], 256, "%s", new_owner);
        return 0;
    }
    for (size_t i = 0; i < pilot_signal_count; ++i) {
        const pilot_signal *s = &pilot_signals[i];
        const pilot_service *svc = &pilot_services[s->service];
        subscription sub = c->subscriptions[i];
        if (!sub.callback || strcmp(sender, c->owners[s->service]) || !dbus_message_has_path(msg, svc->path) ||
            !dbus_message_is_signal(msg, svc->interface, s->name)) continue;
        if (!dbus_message_has_signature(msg, s->signature)) return fail(e, PILOT_BAD_REPLY, NULL, "Signal signature mismatch");
        pilot_reply *reply = NULL;
        int rc = decode_reply(msg, &reply, e);
        if (rc != PILOT_OK) return rc;
        rc = sub.callback(i, reply, sub.userdata, e);
        pilot_reply_free(reply);
        return rc < 0 ? rc : 1;
    }
    return 0;
}
int pilot_poll(pilot_client *c, int timeout_ms, pilot_error *e)
{
    clear_error(e);
    if (!c || c->polling || timeout_ms < 0 || timeout_ms > 60000) return fail(e, PILOT_INVALID_ARGUMENT, NULL, NULL);
    if (!connected(c)) return fail(e, PILOT_DISCONNECTED, NULL, NULL);
    c->polling = true;
    DBusMessage *message = dbus_connection_pop_message(c->events);
    if (!message) {
        dbus_connection_read_write(c->events, timeout_ms);
        message = dbus_connection_pop_message(c->events);
    }
    int callbacks = 0;
    for (unsigned i = 0; message && i < 64; ++i) {
        int rc = dispatch_signal(c, message, e);
        dbus_message_unref(message);
        if (rc < 0) { c->polling = false; return rc; }
        callbacks += rc;
        if (i < 63) message = dbus_connection_pop_message(c->events);
    }
    c->polling = false;
    if (!connected(c)) return fail(e, PILOT_DISCONNECTED, NULL, NULL);
    return callbacks;
}
const char *pilot_status_string(int status)
{
    switch (status) {
    case PILOT_OK: return "ok";
    case PILOT_INVALID_ARGUMENT: return "invalid argument";
    case PILOT_NO_MEMORY: return "out of memory";
    case PILOT_DISCONNECTED: return "device service connection lost";
    case PILOT_TIMEOUT: return "request timeout (execution outcome may be unknown)";
    case PILOT_NOT_SUPPORTED: return "capability unavailable";
    case PILOT_ACCESS_DENIED: return "access denied";
    case PILOT_REMOTE_ERROR: return "device service error";
    case PILOT_BAD_REPLY: return "incompatible or invalid reply";
    case PILOT_BUSY: return "resource or update budget busy";
    default: return "unknown error";
    }
}

size_t pilot_properties_count(const pilot_properties *p)
{
    const pilot_value *v = (const pilot_value *)p;
    return v ? v->count : 0;
}
const char *pilot_properties_key(const pilot_properties *p, size_t i)
{
    const pilot_value *entry = pilot_value_at((const pilot_value *)p, i);
    const pilot_value *key = pilot_value_at(entry, 0);
    return key && key->type == 's' ? key->as.string : NULL;
}
bool pilot_properties_bool(const pilot_properties *p, const char *key, bool *out)
{
    const pilot_value *v = pilot_dict_get((const pilot_value *)p, key);
    if (!v || !out || v->type != 'b') return false;
    *out = v->as.boolean; return true;
}
bool pilot_properties_int(const pilot_properties *p, const char *key, int64_t *out)
{
    const pilot_value *v = pilot_dict_get((const pilot_value *)p, key);
    if (!v || !out) return false;
    switch (v->type) {
    case 'n': *out = v->as.i16; return true;
    case 'i': *out = v->as.i32; return true;
    case 'x': *out = v->as.i64; return true;
    default: return false;
    }
}
bool pilot_properties_uint(const pilot_properties *p, const char *key, uint64_t *out)
{
    const pilot_value *v = pilot_dict_get((const pilot_value *)p, key);
    if (!v || !out) return false;
    switch (v->type) {
    case 'y': *out = v->as.byte; return true;
    case 'q': *out = v->as.u16; return true;
    case 'u': *out = v->as.u32; return true;
    case 't': *out = v->as.u64; return true;
    default: return false;
    }
}
bool pilot_properties_double(const pilot_properties *p, const char *key, double *out)
{
    const pilot_value *v = pilot_dict_get((const pilot_value *)p, key);
    if (!v || !out || v->type != 'd') return false;
    *out = v->as.real; return true;
}
bool pilot_properties_string(const pilot_properties *p, const char *key, const char **out)
{
    const pilot_value *v = pilot_dict_get((const pilot_value *)p, key);
    if (!v || !out || v->type != 's') return false;
    *out = v->as.string; return true;
}
