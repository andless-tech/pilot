#define _POSIX_C_SOURCE 200809L
#include "pilot/api.h"
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t running = 1;
static void stop(int sig) { (void)sig; running = 0; }
static void report(const char *operation, int rc, const pilot_error *e)
{
    fprintf(stderr, "%s: %s (%s: %s)\n", operation, pilot_status_string(rc), e->name, e->message);
}
static void changed(unsigned id, const pilot_reply *reply, void *userdata)
{
    (void)userdata;
    if (id == PILOT_SIGNAL_RTC_IMU_DATA_CHANGED) {
        /* The signal has the same primitive fields as GetImuData. */
        const pilot_value *valid = pilot_reply_at(reply, 1);
        printf("IMU valid=%d gyro=%.3f %.3f %.3f accel=%.3f %.3f %.3f\n",
               valid->as.boolean, pilot_reply_at(reply, 2)->as.real,
               pilot_reply_at(reply, 3)->as.real, pilot_reply_at(reply, 4)->as.real,
               pilot_reply_at(reply, 5)->as.real, pilot_reply_at(reply, 6)->as.real,
               pilot_reply_at(reply, 7)->as.real);
    } else if (id == PILOT_SIGNAL_SELFCHECK_REPORT_CHANGED || id == PILOT_SIGNAL_MODEM_CELL_INFO_CHANGED) {
        printf("%s: %s\n", pilot_signals[id].name, pilot_reply_at(reply, 0)->as.string);
    } else {
        printf("%s (%zu fields)\n", pilot_signals[id].name, pilot_reply_count(reply));
    }
}
int main(int argc, char **argv)
{
    pilot_options options = {0};
    bool monitor = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--session")) options.session_bus = true;
        else if (!strcmp(argv[i], "--monitor")) monitor = true;
        else { fprintf(stderr, "Usage: %s [--monitor] [--session]\n", argv[0]); return 2; }
    }
    pilot_client *client = NULL;
    pilot_error error;
    int rc = pilot_open(&options, &client, &error);
    if (rc != PILOT_OK) { report("open", rc, &error); return 1; }
    for (unsigned i = 0; i < pilot_service_count; ++i) {
        bool available;
        rc = pilot_service_available(client, i, &available, &error);
        if (rc == PILOT_OK) printf("%s: %s\n", pilot_services[i].name, available ? "online" : "unavailable");
        else report("service", rc, &error);
    }
    pilot_rtc_get_imu_data_result imu = {0};
    rc = pilot_rtc_get_imu_data(client, &imu, &error);
    if (rc == PILOT_OK) {
        printf("IMU timestamp=%" PRId64 " valid=%d gyro=%.3f %.3f %.3f\n",
               imu.last_sample_ms, imu.valid, imu.gyro_x, imu.gyro_y, imu.gyro_z);
        pilot_rtc_get_imu_data_clear(&imu);
    } else report("GetImuData", rc, &error);
    pilot_rtc_get_audio_volume_result volume = {0};
    rc = pilot_rtc_get_audio_volume(client, &volume, &error);
    if (rc == PILOT_OK) {
        printf("Audio available=%d input=%u output=%u\n", volume.available, volume.input_volume, volume.output_volume);
        pilot_rtc_get_audio_volume_clear(&volume);
    } else report("GetAudioVolume", rc, &error);
    pilot_rtc_battery_command_result battery = {0};
    rc = pilot_rtc_battery_command(client, "{\"version\":1,\"op\":\"get_profile\"}", &battery, &error);
    if (rc == PILOT_OK) {
        printf("Battery: %s\n", battery.response);
        pilot_rtc_battery_command_clear(&battery);
    } else report("BatteryCommand", rc, &error);
    pilot_rtc_get_gps_status_result gps = {0};
    rc = pilot_rtc_get_gps_status(client, &gps, &error);
    if (rc == PILOT_OK) {
        printf("GPS status dictionary: %zu fields\n", gps.status->count);
        pilot_rtc_get_gps_status_clear(&gps);
    } else report("GetGpsStatus", rc, &error);
    if (monitor) {
        signal(SIGINT, stop); signal(SIGTERM, stop);
        unsigned ids[] = {PILOT_SIGNAL_RTC_IMU_DATA_CHANGED, PILOT_SIGNAL_RTC_MAGNETOMETER_DATA_CHANGED,
                          PILOT_SIGNAL_SELFCHECK_REPORT_CHANGED, PILOT_SIGNAL_MODEM_CELL_INFO_CHANGED};
        for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
            rc = pilot_subscribe(client, ids[i], changed, NULL, &error);
            if (rc != PILOT_OK) { report("subscribe", rc, &error); pilot_close(client); return 1; }
        }
        while (running) {
            rc = pilot_poll(client, 250, &error);
            if (rc >= 0) continue;
            report("poll", rc, &error);
            if (rc == PILOT_DISCONNECTED) {
                do {
                    struct timespec delay = {1, 0};
                    nanosleep(&delay, NULL);
                    rc = pilot_reconnect(client, &error);
                } while (running && rc != PILOT_OK);
            } else break;
        }
    }
    pilot_close(client);
    return rc < 0 ? 1 : 0;
}
