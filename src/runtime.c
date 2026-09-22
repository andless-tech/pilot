#include "private/runtime.h"
#include <dbus/dbus.h>
#include <pthread.h>
static pthread_once_t once = PTHREAD_ONCE_INIT;
static int ready;
static void initialize(void) {
    ready = dbus_threads_init_default();
}
int pilot_private_init_threads(void) {
    pthread_once(&once, initialize);
    return ready;
}
