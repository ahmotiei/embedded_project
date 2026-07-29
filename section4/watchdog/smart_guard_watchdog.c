#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define DEFAULT_HEARTBEAT_FILE "/run/smart-guard/vision_status.json"
#define DEFAULT_EVENT_FILE "/run/smart-guard/system_event.json"
#define DEFAULT_SERVICE "smart-guard-vision.service"
#define DEFAULT_TIMEOUT_SECONDS 30
#define DEFAULT_STARTUP_GRACE_SECONDS 45
#define DEFAULT_CHECK_INTERVAL_SECONDS 2

static volatile sig_atomic_t g_stop = 0;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    g_stop = 1;
}

static const char *get_env_or_default(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return value == NULL || value[0] == '\0' ? fallback : value;
}

static int get_env_int(const char *name, int fallback, int minimum, int maximum)
{
    const char *text = getenv(name);
    char *end = NULL;
    long value;
    if (text == NULL || text[0] == '\0') {
        return fallback;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < minimum || value > maximum) {
        fprintf(stderr, "Invalid %s=%s; using %d\n", name, text, fallback);
        return fallback;
    }
    return (int)value;
}

static void format_timestamp(char *output, size_t output_size)
{
    time_t now = time(NULL);
    struct tm local_time;
    if (localtime_r(&now, &local_time) == NULL ||
        strftime(output, output_size, "%Y-%m-%dT%H:%M:%S%z", &local_time) == 0U) {
        snprintf(output, output_size, "unknown");
    }
}

static unsigned long long make_event_id(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        return (unsigned long long)time(NULL) * 1000000000ULL;
    }
    return (unsigned long long)now.tv_sec * 1000000000ULL + (unsigned long long)now.tv_nsec;
}

static int ensure_parent_directory(const char *path)
{
    char buffer[PATH_MAX];
    char *slash;
    size_t length = strlen(path);
    if (length >= sizeof(buffer)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(buffer, path, length + 1U);
    slash = strrchr(buffer, '/');
    if (slash == NULL || slash == buffer) {
        return 0;
    }
    *slash = '\0';
    for (char *cursor = buffer + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            *cursor = '\0';
            if (mkdir(buffer, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *cursor = '/';
        }
    }
    if (mkdir(buffer, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static int atomic_write_event(const char *path,
                              double age_seconds,
                              const char *service,
                              const char *message)
{
    char temporary[PATH_MAX];
    char timestamp[64];
    char json[2048];
    FILE *file;
    unsigned long long event_id = make_event_id();
    int length;

    if (ensure_parent_directory(path) != 0) {
        return -1;
    }
    format_timestamp(timestamp, sizeof(timestamp));
    length = snprintf(json, sizeof(json),
        "{\"event_id\":\"%llu\",\"type\":\"camera_tamper\","
        "\"timestamp\":\"%s\",\"message\":\"%s\","
        "\"service\":\"%s\",\"frame_age_seconds\":%.3f}\n",
        event_id, timestamp, message, service, age_seconds);
    if (length < 0 || (size_t)length >= sizeof(json)) {
        errno = EOVERFLOW;
        return -1;
    }
    if (snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", path, (long)getpid()) >=
        (int)sizeof(temporary)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    file = fopen(temporary, "wb");
    if (file == NULL) {
        return -1;
    }
    if (fwrite(json, 1U, (size_t)length, file) != (size_t)length ||
        fflush(file) != 0 || fsync(fileno(file)) != 0 || fchmod(fileno(file), 0644) != 0) {
        int saved_errno = errno;
        fclose(file);
        unlink(temporary);
        errno = saved_errno;
        return -1;
    }
    if (fclose(file) != 0 || rename(temporary, path) != 0) {
        int saved_errno = errno;
        unlink(temporary);
        errno = saved_errno;
        return -1;
    }
    fprintf(stdout, "Watchdog event emitted id=%llu age=%.3fs service=%s\n",
            event_id, age_seconds, service);
    fflush(stdout);
    return 0;
}

static int restart_service(const char *service)
{
    pid_t child = fork();
    int status = 0;
    if (child < 0) {
        return -1;
    }
    if (child == 0) {
        execl("/bin/systemctl", "systemctl", "restart", "--no-block", service, (char *)NULL);
        execl("/usr/bin/systemctl", "systemctl", "restart", "--no-block", service, (char *)NULL);
        _exit(127);
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static void sleep_seconds_interruptible(int seconds)
{
    struct timespec request = {.tv_sec = seconds, .tv_nsec = 0};
    while (!g_stop && nanosleep(&request, &request) != 0 && errno == EINTR) {
        /* continue */
    }
}

int main(void)
{
    const char *heartbeat_file = get_env_or_default("SMART_GUARD_WATCHDOG_HEARTBEAT_FILE",
                                                     DEFAULT_HEARTBEAT_FILE);
    const char *event_file = get_env_or_default("SMART_GUARD_SYSTEM_EVENT_FILE",
                                                 DEFAULT_EVENT_FILE);
    const char *service = get_env_or_default("SMART_GUARD_WATCHDOG_SERVICE", DEFAULT_SERVICE);
    int timeout_seconds = get_env_int("SMART_GUARD_WATCHDOG_TIMEOUT_SECONDS",
                                      DEFAULT_TIMEOUT_SECONDS, 5, 3600);
    int startup_grace_seconds = get_env_int("SMART_GUARD_WATCHDOG_STARTUP_GRACE_SECONDS",
                                            DEFAULT_STARTUP_GRACE_SECONDS, 5, 3600);
    int check_interval_seconds = get_env_int("SMART_GUARD_WATCHDOG_CHECK_INTERVAL_SECONDS",
                                             DEFAULT_CHECK_INTERVAL_SECONDS, 1, 60);
    struct sigaction action;
    time_t started_at = time(NULL);
    time_t incident_started_at = 0;
    bool incident_active = false;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    fprintf(stdout,
            "Smart Guard software watchdog started heartbeat=%s timeout=%ds startup_grace=%ds service=%s\n",
            heartbeat_file, timeout_seconds, startup_grace_seconds, service);
    fflush(stdout);

    while (!g_stop) {
        struct stat status;
        time_t now = time(NULL);
        bool heartbeat_exists = stat(heartbeat_file, &status) == 0;
        double age_seconds = heartbeat_exists ? difftime(now, status.st_mtime) : -1.0;
        bool startup_grace_done = difftime(now, started_at) >= (double)startup_grace_seconds;
        bool stale = startup_grace_done && (!heartbeat_exists || age_seconds > (double)timeout_seconds);

        if (incident_active && heartbeat_exists && status.st_mtime > incident_started_at &&
            age_seconds >= 0.0 && age_seconds <= (double)check_interval_seconds * 2.5) {
            incident_active = false;
            fprintf(stdout, "Watchdog recovery: fresh frame heartbeat received\n");
            fflush(stdout);
        }

        if (stale && !incident_active) {
            const char *message = heartbeat_exists
                ? "No new processed camera frame for more than the configured timeout"
                : "Vision heartbeat file is missing after startup grace";
            incident_active = true;
            incident_started_at = now;
            if (atomic_write_event(event_file, age_seconds, service, message) != 0) {
                fprintf(stderr, "Cannot write watchdog event: %s\n", strerror(errno));
            }
            if (restart_service(service) != 0) {
                fprintf(stderr, "Cannot restart %s: %s\n", service, strerror(errno));
            } else {
                fprintf(stdout, "Watchdog requested restart of %s\n", service);
                fflush(stdout);
            }
        }

        sleep_seconds_interruptible(check_interval_seconds);
    }

    fprintf(stdout, "Smart Guard software watchdog stopped\n");
    return EXIT_SUCCESS;
}
