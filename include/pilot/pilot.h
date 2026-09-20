#ifndef PILOT_H
#define PILOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PILOT_VERSION "0.1.0"
#define PILOT_DEFAULT_TIMEOUT_MS 2000

typedef struct pilot_client pilot_client;
typedef struct pilot_reply pilot_reply;
typedef struct pilot_value pilot_value;

typedef enum {
    PILOT_OK = 0, PILOT_INVALID_ARGUMENT = -1, PILOT_NO_MEMORY = -2,
    PILOT_DISCONNECTED = -3, PILOT_TIMEOUT = -4, PILOT_NOT_SUPPORTED = -5,
    PILOT_ACCESS_DENIED = -6, PILOT_REMOTE_ERROR = -7, PILOT_BAD_REPLY = -8
} pilot_status;

typedef struct {
    int code;
    char name[128];
    char message[384];
} pilot_error;

typedef struct {
    bool session_bus; /* Host tests only. Defaults to the board system bus. */
    int timeout_ms;   /* 0 selects 2000 ms; explicit values must be 1..60000. */
} pilot_options;

/* D-Bus signature characters are preserved. Strings/bytes/children are borrowed. */
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
    const pilot_value *items; /* a, r, e, v; use bytes for ay. */
    const uint8_t *bytes;     /* Zero-copy ay, including camera previews. */
};

typedef struct {
    const char *name;
    const char *path;
    const char *interface;
} pilot_service;

typedef struct {
    unsigned service;
    const char *name;
    const char *input_signature;
    const char *output_signature;
} pilot_method;

typedef struct {
    unsigned service;
    const char *name;
    const char *signature;
} pilot_signal;

typedef void (*pilot_signal_callback)(unsigned signal_id,
                                      const pilot_reply *payload, void *userdata);

/* One client belongs to one thread. Callbacks run in pilot_poll, without locks.
 * Use another client for concurrent work. Do not close/reconnect/poll recursively
 * in callbacks. No background thread, no automatic replay of mutations. */
int pilot_open(const pilot_options *options, pilot_client **out, pilot_error *error);
void pilot_close(pilot_client *client);
int pilot_reconnect(pilot_client *client, pilot_error *error);
int pilot_service_available(pilot_client *client, unsigned service,
                            bool *available, pilot_error *error);
int pilot_introspect(pilot_client *client, unsigned service,
                     pilot_reply **reply, pilot_error *error);
int pilot_call(pilot_client *client, unsigned method, const pilot_value *args,
               size_t count, pilot_reply **reply, pilot_error *error);
int pilot_subscribe(pilot_client *client, unsigned signal_id,
                    pilot_signal_callback callback, void *userdata, pilot_error *error);
int pilot_unsubscribe(pilot_client *client, unsigned signal_id, pilot_error *error);
/* Returns callback count, or a negative pilot_status. At most 64 bus messages
 * per call; timeout_ms is 0..60000. Reconnect explicitly after DISCONNECTED. */
int pilot_poll(pilot_client *client, int timeout_ms, pilot_error *error);

size_t pilot_reply_count(const pilot_reply *reply);
const pilot_value *pilot_reply_at(const pilot_reply *reply, size_t index);
void pilot_reply_free(pilot_reply *reply);
const pilot_value *pilot_value_at(const pilot_value *value, size_t index);
const pilot_value *pilot_value_unwrap(const pilot_value *value);
const pilot_value *pilot_dict_get(const pilot_value *dict, const char *key);
const char *pilot_status_string(int status);

extern const pilot_service pilot_services[];
extern const size_t pilot_service_count;
extern const pilot_method pilot_methods[];
extern const size_t pilot_method_count;
extern const pilot_signal pilot_signals[];
extern const size_t pilot_signal_count;

#ifdef __cplusplus
}
#endif
#endif
