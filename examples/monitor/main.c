#define _POSIX_C_SOURCE 200809L
#include <pilot/api.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t running = 1;
static void stop(int sig) { (void)sig; running = 0; }
static void report(const char *operation, int rc, const pilot_error *error)
{
    fprintf(stderr, "%s: %s (%s)\n", operation, pilot_status_string(rc), error->message);
}
static void on_imu(const pilot_imu_event *event, void *userdata)
{
    (void)userdata;
    printf("IMU valid=%d gyro=%.3f %.3f %.3f accel=%.3f %.3f %.3f\n",
           event->has_imu, event->gyro_x, event->gyro_y, event->gyro_z,
           event->accel_x, event->accel_y, event->accel_z);
}
int main(int argc, char **argv)
{
    bool monitor = argc == 2 && !strcmp(argv[1], "--monitor");
    if (argc > 2 || (argc == 2 && !monitor)) {
        fprintf(stderr, "Usage: %s [--monitor]\n", argv[0]); return 2;
    }
    pilot_client *client = NULL;
    pilot_error error;
    int rc = pilot_open(NULL, &client, &error);
    if (rc != PILOT_OK) { report("open", rc, &error); return 1; }
    pilot_imu_read_result imu = {0};
    rc = pilot_imu_read(client, &imu, &error);
    if (rc == PILOT_OK) {
        printf("IMU timestamp=%" PRId64 " valid=%d gyro=%.3f %.3f %.3f\n",
               imu.last_sample_ms, imu.valid, imu.gyro_x, imu.gyro_y, imu.gyro_z);
    } else report("IMU", rc, &error);
    pilot_imu_read_clear(&imu);
    pilot_audio_get_volume_result volume = {0};
    rc = pilot_audio_get_volume(client, &volume, &error);
    if (rc == PILOT_OK) printf("Audio available=%d input=%u output=%u\n",
                               volume.available, volume.input_volume, volume.output_volume);
    else report("audio", rc, &error);
    pilot_audio_get_volume_clear(&volume);
    pilot_gps_read_result gps = {0};
    rc = pilot_gps_read(client, &gps, &error);
    if (rc == PILOT_OK) {
        double latitude, longitude;
        if (pilot_properties_double(gps.status, "latitude", &latitude) &&
            pilot_properties_double(gps.status, "longitude", &longitude))
            printf("GPS %.6f, %.6f\n", latitude, longitude);
    } else report("GPS", rc, &error);
    pilot_gps_read_clear(&gps);
    if (monitor) {
        signal(SIGINT, stop); signal(SIGTERM, stop);
        rc = pilot_watch_imu(client, on_imu, NULL, &error);
        if (rc != PILOT_OK) { report("watch IMU", rc, &error); pilot_close(client); return 1; }
        while (running) {
            rc = pilot_poll(client, 250, &error);
            if (rc >= 0) continue;
            report("poll", rc, &error);
            if (rc != PILOT_DISCONNECTED) break;
            do {
                struct timespec delay = {1, 0};
                nanosleep(&delay, NULL);
                rc = pilot_reconnect(client, &error);
            } while (running && rc != PILOT_OK);
        }
    }
    pilot_close(client);
    return rc < 0 ? 1 : 0;
}
