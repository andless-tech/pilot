#ifndef PILOT_TRANSPORT_H
#define PILOT_TRANSPORT_H
#include "pilot/pilot.h"
/* Private transport ABI. Never install or ship as an application header. */
typedef struct pilot_reply pilot_reply;
typedef struct pilot_value pilot_value;
struct pilot_value {
    char type;
    union {
        bool boolean;
        uint8_t byte;
        int16_t i16;
        uint16_t u16;
        int32_t i32;
        uint32_t u32;
        int64_t i64;
        uint64_t u64;
        double real;
        const char *string;
    } as;
    size_t count;
    const pilot_value *items;
    const uint8_t *bytes;
};
typedef struct { const char *name, *path, *interface; } pilot_service;
typedef struct {
    unsigned service;
    const char *name, *input_signature, *output_signature;
} pilot_method;
typedef struct { unsigned service; const char *name, *signature; } pilot_signal;
typedef int (*pilot_signal_callback)(unsigned id, const pilot_reply *reply, void *userdata, pilot_error *error);
int pilot_open_internal(const pilot_options *options, bool session_bus, pilot_client **out, pilot_error *error);
int pilot_service_available(pilot_client *client, unsigned service, bool *available, pilot_error *error);
int pilot_introspect(pilot_client *client, unsigned service, pilot_reply **reply, pilot_error *error);
int pilot_call(pilot_client *client, unsigned method, const pilot_value *args, size_t count, pilot_reply **reply, pilot_error *error);
int pilot_subscribe(pilot_client *client, unsigned id, pilot_signal_callback callback, void *userdata, pilot_error *error);
int pilot_watch_install(pilot_client *client, unsigned id, pilot_signal_callback callback, void *owned_context, pilot_error *error);
int pilot_unsubscribe(pilot_client *client, unsigned id, pilot_error *error);
size_t pilot_reply_count(const pilot_reply *reply);
const pilot_value *pilot_reply_at(const pilot_reply *reply, size_t index);
void pilot_reply_free(pilot_reply *reply);
const pilot_value *pilot_value_at(const pilot_value *value, size_t index);
const pilot_value *pilot_value_unwrap(const pilot_value *value);
const pilot_value *pilot_dict_get(const pilot_value *dict, const char *key);
extern const pilot_service pilot_services[];
extern const size_t pilot_service_count;
extern const pilot_method pilot_methods[];
extern const size_t pilot_method_count;
extern const pilot_signal pilot_signals[];
extern const size_t pilot_signal_count;
#endif
