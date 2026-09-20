#define _POSIX_C_SOURCE 200809L
#include "pilot/api.h"
#include "../src/private/protocol.h"
#include <dbus/dbus.h>
#include <assert.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const uint64_t large_number = UINT64_C(9007199254740999);
static bool empty_arrays;
static void append_signature(DBusMessageIter *out, DBusSignatureIter *sig)
{
    do {
        int type = dbus_signature_iter_get_current_type(sig);
        if (type == DBUS_TYPE_INVALID) break;
        DBusMessageIter child;
        DBusSignatureIter nested;
        switch (type) {
        case DBUS_TYPE_ARRAY: {
            dbus_signature_iter_recurse(sig, &nested);
            char *element = dbus_signature_iter_get_signature(&nested);
            assert(dbus_message_iter_open_container(out, type, element, &child));
            dbus_free(element);
            if (!empty_arrays) {
                append_signature(&child, &nested);
                dbus_signature_iter_recurse(sig, &nested);
                append_signature(&child, &nested);
            }
            assert(dbus_message_iter_close_container(out, &child));
            break;
        }
        case DBUS_TYPE_DICT_ENTRY: case DBUS_TYPE_STRUCT:
            assert(dbus_message_iter_open_container(out, type, NULL, &child));
            dbus_signature_iter_recurse(sig, &nested);
            append_signature(&child, &nested);
            assert(dbus_message_iter_close_container(out, &child));
            break;
        case DBUS_TYPE_VARIANT: {
            assert(dbus_message_iter_open_container(out, type, "d", &child));
            double value = 1.25;
            assert(dbus_message_iter_append_basic(&child, DBUS_TYPE_DOUBLE, &value));
            assert(dbus_message_iter_close_container(out, &child));
            break;
        }
        case DBUS_TYPE_STRING: { const char *v = "sample"; assert(dbus_message_iter_append_basic(out, type, &v)); break; }
        case DBUS_TYPE_BOOLEAN: { dbus_bool_t v = TRUE; assert(dbus_message_iter_append_basic(out, type, &v)); break; }
        case DBUS_TYPE_BYTE: { uint8_t v = 231; assert(dbus_message_iter_append_basic(out, type, &v)); break; }
        case DBUS_TYPE_UINT16: { uint16_t v = 1234; assert(dbus_message_iter_append_basic(out, type, &v)); break; }
        case DBUS_TYPE_INT32: { int32_t v = -23; assert(dbus_message_iter_append_basic(out, type, &v)); break; }
        case DBUS_TYPE_UINT32: { uint32_t v = 42; assert(dbus_message_iter_append_basic(out, type, &v)); break; }
        case DBUS_TYPE_INT64: { int64_t v = -9007199254740999LL; assert(dbus_message_iter_append_basic(out, type, &v)); break; }
        case DBUS_TYPE_UINT64: assert(dbus_message_iter_append_basic(out, type, &large_number)); break;
        case DBUS_TYPE_DOUBLE: { double v = 1.25; assert(dbus_message_iter_append_basic(out, type, &v)); break; }
        default: assert(!"Unhandled mock type");
        }
    } while (dbus_signature_iter_next(sig));
}
static void fill_message(DBusMessage *msg, const char *signature)
{
    DBusMessageIter out;
    DBusSignatureIter sig;
    dbus_message_iter_init_append(msg, &out);
    dbus_signature_iter_init(&sig, signature);
    append_signature(&out, &sig);
}
static DBusConnection *mock_connect(void)
{
    DBusConnection *bus = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);
    assert(bus);
    dbus_connection_set_exit_on_disconnect(bus, FALSE);
    for (size_t i = 0; i < pilot_service_count; ++i)
        assert(dbus_bus_request_name(bus, pilot_services[i].name, DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);
    return bus;
}
static void emit_signals(DBusConnection *bus, bool bad, bool spoof)
{
    DBusConnection *sender = spoof ? dbus_bus_get_private(DBUS_BUS_SESSION, NULL) : bus;
    assert(sender);
    for (size_t i = 0; i < pilot_signal_count; ++i) {
        const pilot_signal *sig = &pilot_signals[i];
        const pilot_service *svc = &pilot_services[sig->service];
        DBusMessage *msg = dbus_message_new_signal(svc->path, svc->interface, sig->name);
        assert(msg);
        fill_message(msg, bad ? "s" : sig->signature);
        assert(dbus_connection_send(sender, msg, NULL));
        dbus_message_unref(msg);
        if (bad) break;
    }
    dbus_connection_flush(sender);
    if (spoof) { dbus_connection_close(sender); dbus_connection_unref(sender); }
}
static void mock_server(int ready)
{
    DBusConnection *bus = mock_connect();
    assert(write(ready, "R", 1) == 1);
    close(ready);
    for (;;) {
        dbus_connection_read_write(bus, 100);
        DBusMessage *msg;
        while ((msg = dbus_connection_pop_message(bus))) {
            if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL) { dbus_message_unref(msg); continue; }
            DBusMessage *reply = NULL;
            bool restart = false;
            if (dbus_message_is_method_call(msg, DBUS_INTERFACE_INTROSPECTABLE, "Introspect")) {
                reply = dbus_message_new_method_return(msg);
                const char *xml = "<node/>";
                assert(dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID));
            } else for (size_t i = 0; i < pilot_method_count; ++i) {
                const pilot_method *m = &pilot_methods[i];
                const pilot_service *s = &pilot_services[m->service];
                if (!dbus_message_has_path(msg, s->path) || !dbus_message_is_method_call(msg, s->interface, m->name)) continue;
                assert(dbus_message_has_signature(msg, m->input_signature));
                const char *command = "";
                if (!strcmp(m->name, "BatteryCommand")) assert(dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &command, DBUS_TYPE_INVALID));
                if (!strcmp(command, "__timeout__")) break;
                if (!strcmp(command, "__empty_arrays__")) empty_arrays = true;
                if (!strcmp(command, "__normal_arrays__")) empty_arrays = false;
                if (!strcmp(command, "__denied__")) reply = dbus_message_new_error(msg, DBUS_ERROR_ACCESS_DENIED, "test access denied");
                else if (!strcmp(command, "__unsupported__")) reply = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD, "test old firmware");
                else {
                    reply = dbus_message_new_method_return(msg);
                    fill_message(reply, !strcmp(command, "__bad_reply__") ? "i" : m->output_signature);
                }
                if (!strcmp(command, "__signals__")) emit_signals(bus, false, false);
                if (!strcmp(command, "__spoof__")) emit_signals(bus, false, true);
                if (!strcmp(command, "__bad_signal__")) emit_signals(bus, true, false);
                if (!strcmp(command, "__drop_modem__")) dbus_bus_release_name(bus, pilot_services[PILOT_SERVICE_MODEM].name, NULL);
                if (!strcmp(command, "__restore_modem__")) dbus_bus_request_name(bus, pilot_services[PILOT_SERVICE_MODEM].name, DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL);
                if (!strcmp(command, "__restart__")) restart = true;
                break;
            }
            if (reply) { assert(dbus_connection_send(bus, reply, NULL)); dbus_message_unref(reply); dbus_connection_flush(bus); }
            dbus_message_unref(msg);
            if (restart) { dbus_connection_close(bus); dbus_connection_unref(bus); bus = mock_connect(); }
        }
    }
}

static void check_value(const pilot_value *v)
{
    assert(v);
    switch (v->type) {
    case 's': assert(!strcmp(v->as.string, "sample")); break;
    case 'b': assert(v->as.boolean); break;
    case 'i': assert(v->as.i32 == -23); break;
    case 'u': assert(v->as.u32 == 42); break;
    case 'q': assert(v->as.u16 == 1234); break;
    case 't': assert(v->as.u64 == large_number); break;
    case 'x': assert(v->as.i64 == -9007199254740999LL); break;
    case 'd': assert(v->as.real == 1.25); break;
    case 'a': if (v->bytes) { assert(v->count == 2 && v->bytes[0] == 231); return; } /* fall through */
    case 'r': case 'e': case 'v':
        assert(v->count);
        for (size_t i = 0; i < v->count; ++i) check_value(pilot_value_at(v, i));
        break;
    default: assert(!"Unknown decoded type");
    }
}
static unsigned received[16];
static int callback(unsigned id, const pilot_reply *reply, void *context, pilot_error *error)
{
    (void)error;
    assert(context == received);
    received[id]++;
    for (size_t i = 0; i < pilot_reply_count(reply); ++i) check_value(pilot_reply_at(reply, i));
    return PILOT_OK;
}
static int command(pilot_client *c, const char *name)
{
    pilot_battery_request_result out = {0};
    pilot_error error;
    int rc = pilot_battery_request(c, name, &out, &error);
    assert(error.code == rc);
    pilot_battery_request_clear(&out);
    return rc;
}
static void drain(pilot_client *c)
{
    pilot_error error;
    for (int i = 0; i < 20; ++i) assert(pilot_poll(c, 10, &error) >= 0);
}
#include "public_calls.h"
int main(int argc, char **argv)
{
    alarm(30);
    int ready[2]; assert(pipe(ready) == 0);
    pid_t child = fork(); assert(child >= 0);
    if (!child) { close(ready[0]); mock_server(ready[1]); return 0; }
    close(ready[1]); char byte; assert(read(ready[0], &byte, 1) == 1); close(ready[0]);
    pilot_client *c = NULL;
    pilot_error error;
    pilot_options options = {.timeout_ms = 150};
    assert(pilot_open_internal(&options, true, &c, &error) == PILOT_OK);
    for (unsigned i = 0; i < pilot_service_count; ++i) {
        bool available = false;
        assert(pilot_service_available(c, i, &available, &error) == PILOT_OK && available);
        pilot_reply *reply = NULL;
        assert(pilot_introspect(c, i, &reply, &error) == PILOT_OK);
        assert(!strcmp(pilot_reply_at(reply, 0)->as.string, "<node/>")); pilot_reply_free(reply);
    }
    for (unsigned i = 0; i < pilot_method_count; ++i) {
        pilot_value args[8] = {0};
        size_t count = strlen(pilot_methods[i].input_signature);
        for (size_t j = 0; j < count; ++j) {
            args[j].type = pilot_methods[i].input_signature[j];
            if (args[j].type == 's') args[j].as.string = "sample";
            else if (args[j].type == 'b') args[j].as.boolean = true;
            else if (args[j].type == 'u') args[j].as.u32 = 42;
            else args[j].as.i32 = -23;
        }
        pilot_reply *reply = NULL;
        assert(pilot_call(c, i, args, count, &reply, &error) == PILOT_OK);
        for (size_t j = 0; j < pilot_reply_count(reply); ++j) check_value(pilot_reply_at(reply, j));
        pilot_reply_free(reply);
    }
    pilot_imu_read_result imu = {0};
    assert(pilot_imu_read(c, &imu, &error) == PILOT_OK);
    assert(imu.valid && imu.gyro_x == 1.25 && imu.last_sample_ms == -9007199254740999LL);
    pilot_imu_read_clear(&imu); pilot_imu_read_clear(&imu);
    assert(pilot_imu_read(c, NULL, &error) == PILOT_INVALID_ARGUMENT && error.code == PILOT_INVALID_ARGUMENT);
    pilot_gps_read_result gps = {0};
    assert(pilot_gps_read(c, &gps, &error) == PILOT_OK);
    double value = 0;
    assert(pilot_properties_double(gps.status, "sample", &value) && value == 1.25);
    assert(!pilot_properties_double(gps.status, "missing", &value));
    assert(pilot_properties_count(gps.status) == 2);
    assert(!strcmp(pilot_properties_key(gps.status, 0), "sample"));
    bool flag;
    assert(!pilot_properties_bool(gps.status, "sample", &flag));
    assert(!pilot_properties_double(gps.status, "sample", NULL));
    pilot_gps_read_clear(&gps);
    pilot_audio_set_volume_result volume = {0};
    assert(pilot_audio_set_volume(c, "input", 60, &volume, &error) == PILOT_OK);
    assert(volume.accepted && volume.actual_volume == 42); pilot_audio_set_volume_clear(&volume);
    assert(command(c, "__bad_reply__") == PILOT_BAD_REPLY);
    assert(command(c, "__timeout__") == PILOT_TIMEOUT);
    assert(command(c, "__denied__") == PILOT_ACCESS_DENIED);
    assert(command(c, "__unsupported__") == PILOT_NOT_SUPPORTED);
    assert(command(c, NULL) == PILOT_INVALID_ARGUMENT);
    assert(command(c, "\xff") == PILOT_INVALID_ARGUMENT);
    assert(command(c, "__empty_arrays__") == PILOT_OK);
    pilot_audio_read_spectrum_result spectrum = {0};
    assert(pilot_audio_read_spectrum(c, &spectrum, &error) == PILOT_OK && spectrum.levels.size == 0);
    pilot_audio_read_spectrum_clear(&spectrum);
    pilot_connection_get_status_result endpoints = {0};
    assert(pilot_connection_get_status(c, &endpoints, &error) == PILOT_OK && endpoints.endpoints_count == 0);
    pilot_connection_get_status_clear(&endpoints);
    assert(command(c, "__normal_arrays__") == PILOT_OK);
    pilot_reply *reply = NULL;
    assert(pilot_call(c, 999, NULL, 0, &reply, &error) == PILOT_INVALID_ARGUMENT);
    assert(pilot_call(c, PILOT_METHOD_RTC_SET_AUDIO_VOLUME, NULL, 2, &reply, &error) == PILOT_INVALID_ARGUMENT);
    for (unsigned i = 0; i < pilot_signal_count; ++i) {
        assert(pilot_subscribe(c, i, callback, received, &error) == PILOT_OK);
        assert(pilot_subscribe(c, i, callback, received, &error) == PILOT_OK);
    }
    assert(command(c, "__signals__") == PILOT_OK); drain(c);
    for (unsigned i = 0; i < pilot_signal_count; ++i) assert(received[i] == 1);
    assert(command(c, "__spoof__") == PILOT_OK); drain(c);
    for (unsigned i = 0; i < pilot_signal_count; ++i) assert(received[i] == 1);
    assert(command(c, "__bad_signal__") == PILOT_OK);
    bool bad_signal = false;
    for (int i = 0; i < 20; ++i) if (pilot_poll(c, 10, &error) == PILOT_BAD_REPLY) bad_signal = true;
    assert(bad_signal);
    assert(command(c, "__drop_modem__") == PILOT_OK);
    bool available = true;
    assert(pilot_service_available(c, PILOT_SERVICE_MODEM, &available, &error) == PILOT_OK && !available);
    assert(command(c, "__restore_modem__") == PILOT_OK); drain(c);
    assert(command(c, "__restart__") == PILOT_OK); drain(c);
    assert(command(c, "__signals__") == PILOT_OK); drain(c);
    for (unsigned i = 0; i < pilot_signal_count; ++i) assert(received[i] == 2);
    assert(pilot_reconnect(c, &error) == PILOT_OK);
    assert(command(c, "__signals__") == PILOT_OK); drain(c);
    for (unsigned i = 0; i < pilot_signal_count; ++i) assert(received[i] == 3);
    for (unsigned i = 0; i < pilot_signal_count; ++i) assert(pilot_unsubscribe(c, i, &error) == PILOT_OK);
    assert(command(c, "__signals__") == PILOT_OK); drain(c);
    for (unsigned i = 0; i < pilot_signal_count; ++i) assert(received[i] == 3);
    exercise_public_calls(c);
    exercise_public_watches(c, true);
    assert(command(c, "__signals__") == PILOT_OK); drain(c);
    for (unsigned i = 0; i < pilot_signal_count; ++i) assert(public_event_counts[i] == 1);
    exercise_public_watches(c, true);
    assert(pilot_reconnect(c, &error) == PILOT_OK);
    assert(command(c, "__signals__") == PILOT_OK); drain(c);
    for (unsigned i = 0; i < pilot_signal_count; ++i) assert(public_event_counts[i] == 2);
    exercise_public_watches(c, false);
    assert(command(c, "__signals__") == PILOT_OK); drain(c);
    for (unsigned i = 0; i < pilot_signal_count; ++i) assert(public_event_counts[i] == 2);
    exercise_public_watches(c, true); /* close must release all owned callback contexts. */
    pilot_close(c);
    if (argc == 2) {
        pid_t consumer = fork(); assert(consumer >= 0);
        if (!consumer) { execl(argv[1], argv[1], (char *)NULL); _exit(127); }
        int status;
        assert(waitpid(consumer, &status, 0) == consumer && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    kill(child, SIGTERM); waitpid(child, NULL, 0);
    printf("PASS: %zu methods, %zu signals, typed APIs, 64-bit values, arrays/dicts/bytes,\n"
           "errors, malformed replies, owner change, reconnect, unsubscribe and sender isolation\n",
           pilot_method_count, pilot_signal_count);
    return 0;
}
