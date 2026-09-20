#include <pilot/api.h>
#include <assert.h>
#include <stdio.h>

static void unused(unsigned id, const pilot_reply *reply, void *data)
{
    (void)id; (void)reply; (void)data;
}
int main(void)
{
    pilot_client *client = NULL;
    pilot_error error;
    pilot_options options = {.session_bus = true, .timeout_ms = 100};
    assert(pilot_open(&options, &client, &error) == PILOT_OK);
    assert(pilot_subscribe(client, PILOT_SIGNAL_MODEM_CELL_INFO_CHANGED, unused, NULL, &error) == PILOT_OK);
    puts("ready"); fflush(stdout);
    assert(getchar() == '\n');
    int rc = 0;
    for (int i = 0; i < 20 && rc != PILOT_DISCONNECTED; ++i) rc = pilot_poll(client, 20, &error);
    assert(rc == PILOT_DISCONNECTED);
    pilot_rtc_get_imu_data_result imu = {0};
    assert(pilot_rtc_get_imu_data(client, &imu, &error) == PILOT_DISCONNECTED);
    assert(!imu._reply && !imu.valid);
    assert(pilot_reconnect(client, &error) < 0);
    puts("disconnected"); fflush(stdout);
    assert(getchar() == '\n');
    assert(pilot_reconnect(client, &error) == PILOT_OK);
    bool available = true;
    assert(pilot_service_available(client, PILOT_SERVICE_RTC, &available, &error) == PILOT_OK && !available);
    assert(pilot_rtc_get_imu_data(client, &imu, &error) == PILOT_NOT_SUPPORTED);
    assert(pilot_poll(client, 0, &error) >= 0);
    pilot_close(client);
    puts("PASS: bus loss, unavailable restart, restored connection and missing service");
    return 0;
}
