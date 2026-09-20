#define _POSIX_C_SOURCE 200809L
#include <pilot/api.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const uint64_t large_number = UINT64_C(9007199254740999);
#include "public_calls.h"
int main(void)
{
    const char *address = getenv("DBUS_SESSION_BUS_ADDRESS");
    assert(address && setenv("DBUS_SYSTEM_BUS_ADDRESS", address, 1) == 0);
    pilot_client *client = NULL;
    pilot_error error;
    assert(pilot_open(NULL, &client, &error) == PILOT_OK);
    exercise_public_calls(client);
    exercise_public_watches(client, true);
    pilot_battery_request_result result = {0};
    assert(pilot_battery_request(client, "__signals__", &result, &error) == PILOT_OK);
    pilot_battery_request_clear(&result);
    for (int i = 0; i < 20; ++i) assert(pilot_poll(client, 10, &error) >= 0);
    for (unsigned i = 0; i < 7; ++i) assert(public_event_counts[i] == 1);
    exercise_public_watches(client, false);
    pilot_close(client);
    puts("PASS: shared-library consumer exercised 32 business calls and 7 typed events");
    return 0;
}
