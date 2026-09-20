#ifndef PILOT_H
#define PILOT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define PILOT_API __attribute__((visibility("default")))
#else
#define PILOT_API
#endif
#ifdef __cplusplus
extern "C" {
#endif

#define PILOT_VERSION "0.2.0"
#define PILOT_DEFAULT_TIMEOUT_MS 2000
typedef struct pilot_client pilot_client;
typedef struct pilot_properties pilot_properties;

typedef enum {
    PILOT_OK = 0, PILOT_INVALID_ARGUMENT = -1, PILOT_NO_MEMORY = -2,
    PILOT_DISCONNECTED = -3, PILOT_TIMEOUT = -4, PILOT_NOT_SUPPORTED = -5,
    PILOT_ACCESS_DENIED = -6, PILOT_REMOTE_ERROR = -7, PILOT_BAD_REPLY = -8
} pilot_status;
typedef struct { int code; char message[384]; } pilot_error;
typedef struct { int timeout_ms; } pilot_options;
typedef struct { const uint8_t *data; size_t size; } pilot_bytes;

typedef struct {
    const char *type;
    bool connected;
    const char *ip;
    uint16_t port;
    int32_t subscribe_count;
    bool is_active;
} pilot_endpoint;
typedef struct {
    uint32_t channel, mode;
    int32_t value;
    bool enabled, digital_supported;
} pilot_channel_output;
typedef struct {
    const char *type;
    bool connected, active;
    int32_t subscribers;
    uint64_t rtt_us, bytes_sent, bytes_lost;
    uint32_t max_datagram_length;
} pilot_transport_stats;

/* One client per thread. Callbacks run during poll. Do not close, reconnect or
 * recursively poll inside callbacks. Writes are never retried automatically. */
PILOT_API int pilot_open(const pilot_options *options, pilot_client **out, pilot_error *error);
PILOT_API void pilot_close(pilot_client *client);
PILOT_API int pilot_reconnect(pilot_client *client, pilot_error *error);
/* Returns delivered event count, or a negative status. timeout_ms: 0..60000. */
PILOT_API int pilot_poll(pilot_client *client, int timeout_ms, pilot_error *error);
PILOT_API const char *pilot_status_string(int status);

/* Optional GPS/media attributes. Getters return false for missing/wrong types;
 * strings remain valid until their result object is cleared. No coercion. */
PILOT_API size_t pilot_properties_count(const pilot_properties *properties);
PILOT_API const char *pilot_properties_key(const pilot_properties *properties, size_t index);
PILOT_API bool pilot_properties_bool(const pilot_properties *properties, const char *key, bool *out);
PILOT_API bool pilot_properties_int(const pilot_properties *properties, const char *key, int64_t *out);
PILOT_API bool pilot_properties_uint(const pilot_properties *properties, const char *key, uint64_t *out);
PILOT_API bool pilot_properties_double(const pilot_properties *properties, const char *key, double *out);
PILOT_API bool pilot_properties_string(const pilot_properties *properties, const char *key, const char **out);

#ifdef __cplusplus
}
#endif
#endif
