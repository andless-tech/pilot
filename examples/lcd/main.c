#define _POSIX_C_SOURCE 200809L
#include <pilot/ui.h>
#include <signal.h>
#include <stdio.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

static pilot_ui *ui;
static pilot_ui_page page_id, dialog_id;
static volatile sig_atomic_t stopped;
static void stop(int sig) {
    (void)sig;
    stopped = 1;
}
static void on_event(const pilot_ui_event *event, void *userdata) {
    (void)userdata;
    if (event->kind == PILOT_UI_ACTION) {
        /* Validate/apply the business action here. Never block this callback. */
        printf("action page=%u control=%u value=%.1f\n", event->page, event->control, event->value);
        int rc = pilot_ui_complete(ui, event->page, event->action_id, true);
        if (rc)
            fprintf(stderr, "action acknowledgement not queued: %d\n", rc);
        if (event->page == page_id && event->control == 4) {
            rc = pilot_ui_show(ui, dialog_id);
            if (rc)
                fprintf(stderr, "dialog not queued: %d\n", rc);
        }
    } else if (event->kind == PILOT_UI_READY)
        printf("page %u ready\n", event->page);
    else if (event->kind == PILOT_UI_ERROR)
        fprintf(stderr, "LCD status: %d\n", event->status);
}
int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    puts("Pilot LCD demo starting");
    struct rlimit memory;
    printf("runtime uid=%lu gid=%lu nice=%d\n", (unsigned long)getuid(),
           (unsigned long)getgid(), getpriority(PRIO_PROCESS, 0));
    if (!getrlimit(RLIMIT_AS, &memory))
        printf("memory limit=%lu bytes\n", (unsigned long)memory.rlim_cur);
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    if (pilot_ui_open(on_event, NULL, &ui)) {
        fputs("LCD connection unavailable\n", stderr);
        return 1;
    }
    const pilot_ui_control controls[] = {
        {.id = 1, .kind = PILOT_UI_TEXT, .label = "状态", .text = "运行中", .enabled = true},
        {.id = 2,
         .kind = PILOT_UI_PROGRESS,
         .label = "任务进度",
         .text = "%",
         .maximum = 100,
         .enabled = true},
        {.id = 3,
         .kind = PILOT_UI_NUMBER,
         .label = "目标值",
         .value = 25,
         .maximum = 100,
         .step = 1,
         .enabled = true},
        {.id = 4, .kind = PILOT_UI_BUTTON, .label = "操作提示", .text = "打开", .enabled = true}};
    pilot_ui_page_options options = {
        .title = "Pilot 示例", .controls = controls, .count = 4, .update_hz = 10};
    int rc = pilot_ui_register(ui, &options, &page_id);
    const pilot_ui_control message = {.id = 1,
                                      .kind = PILOT_UI_TEXT,
                                      .label = "操作结果",
                                      .text = "示例运行正常",
                                      .enabled = true};
    options = (pilot_ui_page_options){
        .title = "Pilot 提示", .controls = &message, .count = 1, .dialog_timeout_ms = 5000};
    if (!rc)
        rc = pilot_ui_register(ui, &options, &dialog_id);
    if (rc) {
        fprintf(stderr, "register: %d\n", rc);
        pilot_ui_close(ui);
        return 1;
    }
    unsigned ticks = 0;
    while (!stopped) {
        /* 50 Hz producer; SDK coalesces to <=10 Hz visible / <=1 Hz hidden. */
        rc = pilot_ui_set_value(ui, page_id, 2, (ticks++ / 5) % 101);
        if (rc && rc != PILOT_UI_BUSY)
            fprintf(stderr, "update: %d\n", rc);
        rc = pilot_ui_poll(ui);
        if (rc && rc != PILOT_UI_BUSY && rc != PILOT_DISCONNECTED)
            break;
        struct timespec delay = {0, 20000000};
        nanosleep(&delay, NULL);
    }
    pilot_ui_close(ui);
    return 0;
}
