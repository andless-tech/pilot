#include <pilot/api.h>
#include "../src/private/protocol.h"
#include <assert.h>
#include <stdio.h>

static void unused(const pilot_cellular_event *event, void *data)
{
    (void)event; (void)data;
}
int main(void)
{
    pilot_client *client = NULL;
    pilot_error error;
    pilot_options options = {.timeout_ms = 100};
    assert(pilot_open_internal(&options, true, &client, &error) == PILOT_OK);
    assert(pilot_watch_cellular(client, unused, NULL, &error) == PILOT_OK);
    puts("ready"); fflush(stdout);
    assert(getchar() == '\n');
    int rc = 0;
    for (int i = 0; i < 20 && rc != PILOT_DISCONNECTED; ++i) rc = pilot_poll(client, 20, &error);
    assert(rc == PILOT_DISCONNECTED);
    pilot_imu_read_result imu = {0};
    assert(pilot_imu_read(client, &imu, &error) == PILOT_DISCONNECTED);
    assert(!imu._private && !imu.valid);
    assert(pilot_reconnect(client, &error) < 0);
    puts("disconnected"); fflush(stdout);
    assert(getchar() == '\n');
    assert(pilot_reconnect(client, &error) == PILOT_OK);
    bool available = true;
    assert(pilot_service_available(client, PILOT_SERVICE_RTC, &available, &error) == PILOT_OK && !available);
    assert(pilot_imu_read(client, &imu, &error) == PILOT_NOT_SUPPORTED);
    assert(pilot_poll(client, 0, &error) >= 0);
    pilot_close(client);
    puts("PASS: bus loss, unavailable restart, restored connection and missing service");
    return 0;
}
