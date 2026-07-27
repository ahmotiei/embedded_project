#define _GNU_SOURCE

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <microhttpd.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define STUDENT_NAME "Amir Hossein Motiei"
#define STUDENT_ID   "401102553"

#define DEFAULT_HTTP_PORT 80
#define DEFAULT_HTTPS_PORT 443
#define DEFAULT_INTERNAL_API_PORT 18080
#define DEFAULT_TEMP_PORT 9090
#define DEFAULT_CAMERA_PORT 9100

#define MAX_FRAME_SIZE (4U * 1024U * 1024U)
#define STREAM_BLOCK_SIZE (64U * 1024U)
#define TEMP_STALE_SECONDS 10.0
#define MAX_POST_BODY 4096U
#define HISTORY_CAPACITY 5U
#define HISTORY_RECORD_INTERVAL_SECONDS 5

#define MJPEG_BOUNDARY "smartguardframe"

#ifndef MHD_HTTP_PAYLOAD_TOO_LARGE
#define MHD_HTTP_PAYLOAD_TOO_LARGE 413
#endif

#ifndef MHD_CONTENT_READER_END_OF_STREAM
#define MHD_CONTENT_READER_END_OF_STREAM ((ssize_t)-1)
#endif
#ifndef MHD_CONTENT_READER_END_WITH_ERROR
#define MHD_CONTENT_READER_END_WITH_ERROR ((ssize_t)-2)
#endif

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned char *data;
    size_t size;
    uint64_t sequence;
    struct timespec received_mono;
    bool camera_connected;
} frame_store_t;

typedef struct {
    pthread_mutex_t mutex;
    bool valid;
    double celsius;
    struct timespec received_mono;
} host_temperature_store_t;

typedef struct {
    pthread_mutex_t mutex;
    bool initialized;
    uint64_t previous_total;
    uint64_t previous_idle;
    double cpu_usage_percent;
    unsigned long long memory_total_kb;
    unsigned long long memory_free_kb;
    unsigned long long memory_available_kb;
    bool temperature_available;
    bool temperature_stale;
    double temperature_c;
    char temperature_source[48];
    char sampled_at[64];
} telemetry_store_t;

typedef struct {
    uint64_t id;
    int persons;
    char timestamp[64];
} history_record_t;

typedef struct {
    pthread_mutex_t mutex;
    history_record_t records[HISTORY_CAPACITY];
    size_t count;
    size_t next;
    uint64_t next_id;
} history_store_t;

typedef struct {
    uint64_t last_sequence;
    unsigned char *chunk;
    size_t chunk_size;
    size_t chunk_offset;
    unsigned int frames_sent;
    unsigned int max_frames;
} stream_context_t;

typedef struct {
    char *body;
    size_t body_size;
    bool handled;
} request_context_t;

typedef enum {
    COMMAND_OK = 0,
    COMMAND_BAD_REQUEST = 1,
    COMMAND_FORBIDDEN = 2,
    COMMAND_FAILED = 3
} command_status_t;

typedef command_status_t (*command_handler_t)(char *result, size_t result_size);

typedef struct {
    const char *name;
    const char *description;
    command_handler_t handler;
} command_entry_t;

static volatile sig_atomic_t g_stop = 0;
static int g_temp_socket = -1;
static int g_camera_listen_socket = -1;

static frame_store_t g_frame = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .data = NULL,
    .size = 0,
    .sequence = 0,
    .camera_connected = false
};

static host_temperature_store_t g_host_temperature = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .valid = false,
    .celsius = 0.0
};

static telemetry_store_t g_telemetry = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .initialized = false,
    .previous_total = 0,
    .previous_idle = 0,
    .cpu_usage_percent = 0.0,
    .memory_total_kb = 0,
    .memory_free_kb = 0,
    .memory_available_kb = 0,
    .temperature_available = false,
    .temperature_stale = true,
    .temperature_c = 0.0,
    .temperature_source = "unavailable",
    .sampled_at = ""
};

static history_store_t g_history = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .count = 0,
    .next = 0,
    .next_id = 1
};

static char g_allowed_host_ip[INET_ADDRSTRLEN] = "192.168.122.1";
static char g_public_host[256] = "192.168.122.186";
static char g_person_file[512] = "/run/smart-guard/person_count";
static char g_latest_frame_file[512] = "/run/smart-guard/latest.jpg";
static char g_placeholder_file[512] = "/opt/smart-guard/share/no-camera.jpg";
static char g_command_token[256] = "";

static int g_http_port = DEFAULT_HTTP_PORT;
static int g_https_port = DEFAULT_HTTPS_PORT;
static int g_internal_api_port = DEFAULT_INTERNAL_API_PORT;
static int g_temp_port = DEFAULT_TEMP_PORT;
static int g_camera_port = DEFAULT_CAMERA_PORT;

static const char INDEX_HTML[] =
"<!doctype html>"
"<html lang=\"en\">"
"<head>"
"<meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>" STUDENT_NAME " - " STUDENT_ID "</title>"
"<style>"
":root{color-scheme:dark;--bg:#0f172a;--card:#182338;--line:#31405a;--text:#edf3ff;--muted:#9fb0ca;--ok:#43d17d;--warn:#ffb84d;}"
"*{box-sizing:border-box}body{margin:0;background:linear-gradient(145deg,#08101f,#142038);font-family:system-ui,-apple-system,Segoe UI,sans-serif;color:var(--text)}"
".wrap{max-width:1180px;margin:auto;padding:24px}.top{display:flex;justify-content:space-between;gap:16px;align-items:center;flex-wrap:wrap;margin-bottom:20px}"
"h1{margin:0;font-size:clamp(1.6rem,4vw,2.6rem)}.sub{color:var(--muted);margin-top:6px}.pill{border:1px solid var(--line);background:#111c30;padding:9px 13px;border-radius:999px}"
".grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:14px}.card{background:rgba(24,35,56,.92);border:1px solid var(--line);border-radius:18px;padding:18px;box-shadow:0 14px 32px rgba(0,0,0,.2)}"
".label{color:var(--muted);font-size:.9rem}.value{font-size:clamp(1.45rem,3.5vw,2.2rem);font-weight:700;margin-top:8px}.camera{margin-top:14px}.camera img{display:block;width:100%;aspect-ratio:16/9;object-fit:contain;background:#05080e;border-radius:14px;border:1px solid var(--line)}"
".footer{margin-top:16px;color:var(--muted);font-size:.9rem}.ok{color:var(--ok)}.warn{color:var(--warn)}.links{margin-top:14px}.links a{color:#8ec5ff;margin-right:18px}"
"@media(max-width:850px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}}@media(max-width:480px){.grid{grid-template-columns:1fr}.wrap{padding:15px}}"
"</style>"
"</head>"
"<body>"
"<main class=\"wrap\">"
"<section class=\"top\"><div><h1>Smart Guard System</h1><div class=\"sub\">Student: " STUDENT_NAME " &nbsp;|&nbsp; ID: " STUDENT_ID "</div></div><div class=\"pill\">Section 2 REST API enabled</div></section>"
"<section class=\"grid\">"
"<article class=\"card\"><div class=\"label\">CPU usage</div><div class=\"value\" id=\"cpu\">--</div></article>"
"<article class=\"card\"><div class=\"label\">Available memory</div><div class=\"value\" id=\"mem\">--</div></article>"
"<article class=\"card\"><div class=\"label\">CPU temperature</div><div class=\"value\" id=\"temp\">--</div></article>"
"<article class=\"card\"><div class=\"label\">Persons detected</div><div class=\"value\" id=\"persons\">0</div></article>"
"</section>"
"<section class=\"card camera\"><div class=\"label\">Live MJPEG stream</div><img src=\"/api/v1/stream\" alt=\"Live camera stream\"></section>"
"<div class=\"links\"><a href=\"https://" "HOST_PLACEHOLDER" ":8443/docs\">Swagger UI (port 8443)</a><a href=\"/api/v1/history\">Last detections</a></div>"
"<div class=\"footer\"><span id=\"status\" class=\"warn\">Waiting for telemetry...</span> &nbsp; Last update: <span id=\"timestamp\">--</span></div>"
"</main>"
"<script>"
"const $=id=>document.getElementById(id);const mb=kb=>(kb/1024).toFixed(1)+' MB';"
"async function update(){try{const [tr,pr]=await Promise.all([fetch('/api/v1/telemetry',{cache:'no-store'}),fetch('/api/v1/persons',{cache:'no-store'})]);if(!tr.ok||!pr.ok)throw new Error('HTTP error');const t=await tr.json(),p=await pr.json();$('cpu').textContent=t.cpu_usage_percent.toFixed(1)+' %';$('mem').textContent=mb(t.memory_available_kb);$('persons').textContent=p.persons;if(t.cpu_temperature_available){$('temp').textContent=t.cpu_temperature_c.toFixed(1)+' °C';}else{$('temp').textContent='Unavailable';}$('timestamp').textContent=t.timestamp;$('status').textContent=t.camera_connected?'System online':'Waiting for camera';$('status').className=t.camera_connected?'ok':'warn';}catch(e){$('status').textContent='Telemetry error: '+e.message;$('status').className='warn';}}update();setInterval(update,2000);"
"</script>"
"</body></html>";

static void format_timestamp(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm local_tm;
    localtime_r(&now, &local_tm);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%S%z", &local_tm);
}

static double monotonic_age_seconds(struct timespec newer, struct timespec older)
{
    time_t sec = newer.tv_sec - older.tv_sec;
    long nsec = newer.tv_nsec - older.tv_nsec;
    if (nsec < 0) {
        --sec;
        nsec += 1000000000L;
    }
    return (double)sec + (double)nsec / 1000000000.0;
}

static const char *get_env_string(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return (value != NULL && value[0] != '\0') ? value : fallback;
}

static int get_env_port(const char *name, int fallback)
{
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed < 1 || parsed > 65535) {
        fprintf(stderr, "Invalid %s=%s; using %d\n", name, value, fallback);
        return fallback;
    }
    return (int)parsed;
}

static void on_signal(int signo)
{
    (void)signo;
    g_stop = 1;
    if (g_temp_socket >= 0) {
        close(g_temp_socket);
        g_temp_socket = -1;
    }
    if (g_camera_listen_socket >= 0) {
        close(g_camera_listen_socket);
        g_camera_listen_socket = -1;
    }
    pthread_cond_broadcast(&g_frame.cond);
}

static char *read_entire_file(const char *path, size_t *length_out)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char *buffer = calloc((size_t)length + 1U, 1U);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }
    size_t read_size = fread(buffer, 1U, (size_t)length, file);
    fclose(file);
    if (read_size != (size_t)length) {
        free(buffer);
        return NULL;
    }
    if (length_out != NULL) {
        *length_out = read_size;
    }
    return buffer;
}

static bool peer_is_allowed(const struct sockaddr_in *peer)
{
    char address[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &peer->sin_addr, address, sizeof(address)) == NULL) {
        return false;
    }
    return strcmp(address, g_allowed_host_ip) == 0;
}

static int read_cpu_counters(uint64_t *total, uint64_t *idle)
{
    FILE *file = fopen("/proc/stat", "r");
    if (file == NULL) {
        return -1;
    }
    char line[512];
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return -1;
    }
    fclose(file);

    unsigned long long user = 0, nice = 0, system = 0, idle_ticks = 0;
    unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;
    int fields = sscanf(
        line,
        "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
        &user, &nice, &system, &idle_ticks,
        &iowait, &irq, &softirq, &steal
    );
    if (fields < 4) {
        return -1;
    }
    *idle = (uint64_t)(idle_ticks + iowait);
    *total = (uint64_t)(user + nice + system + idle_ticks + iowait + irq + softirq + steal);
    return 0;
}

static int read_memory_kb(
    unsigned long long *total_kb,
    unsigned long long *free_kb,
    unsigned long long *available_kb
)
{
    FILE *file = fopen("/proc/meminfo", "r");
    if (file == NULL) {
        return -1;
    }
    bool have_total = false, have_free = false, have_available = false;
    char line[256];
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned long long value = 0;
        if (sscanf(line, "MemTotal: %llu kB", &value) == 1) {
            *total_kb = value;
            have_total = true;
        } else if (sscanf(line, "MemFree: %llu kB", &value) == 1) {
            *free_kb = value;
            have_free = true;
        } else if (sscanf(line, "MemAvailable: %llu kB", &value) == 1) {
            *available_kb = value;
            have_available = true;
        }
    }
    fclose(file);
    return (have_total && have_free && have_available) ? 0 : -1;
}

static int read_integer_file(const char *path, long long *value)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }
    long long parsed = 0;
    int result = fscanf(file, "%lld", &parsed);
    fclose(file);
    if (result != 1) {
        return -1;
    }
    *value = parsed;
    return 0;
}

static int sensor_score(const char *temperature_path)
{
    int score = 0;
    char metadata_path[1024];
    snprintf(metadata_path, sizeof(metadata_path), "%s", temperature_path);

    char *slash = strrchr(metadata_path, '/');
    if (slash == NULL) {
        return score;
    }
    *slash = '\0';

    const char *names[] = {"type", "name"};
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s", metadata_path, names[index]);
        FILE *file = fopen(path, "r");
        if (file == NULL) {
            continue;
        }
        char text[256];
        if (fgets(text, sizeof(text), file) != NULL) {
            for (char *cursor = text; *cursor != '\0'; ++cursor) {
                *cursor = (char)tolower((unsigned char)*cursor);
            }
            if (strstr(text, "x86_pkg_temp") != NULL ||
                strstr(text, "package") != NULL ||
                strstr(text, "coretemp") != NULL ||
                strstr(text, "k10temp") != NULL ||
                strstr(text, "tctl") != NULL ||
                strstr(text, "tdie") != NULL ||
                strstr(text, "cpu") != NULL ||
                strstr(text, "soc") != NULL) {
                score += 100;
            }
        }
        fclose(file);
    }
    return score;
}

static int find_local_cpu_temperature_millidegrees(long long *temperature)
{
    const char *patterns[] = {
        "/sys/class/thermal/thermal_zone*/temp",
        "/sys/class/hwmon/hwmon*/temp*_input"
    };
    int best_score = -1;
    long long best_value = 0;

    for (size_t p = 0; p < sizeof(patterns) / sizeof(patterns[0]); ++p) {
        glob_t matches;
        memset(&matches, 0, sizeof(matches));
        if (glob(patterns[p], 0, NULL, &matches) != 0) {
            globfree(&matches);
            continue;
        }
        for (size_t index = 0; index < matches.gl_pathc; ++index) {
            long long raw = 0;
            if (read_integer_file(matches.gl_pathv[index], &raw) != 0) {
                continue;
            }
            long long milli = llabs(raw) > 1000LL ? raw : raw * 1000LL;
            if (milli < -50000LL || milli > 200000LL) {
                continue;
            }
            int score = sensor_score(matches.gl_pathv[index]);
            if (score > best_score) {
                best_score = score;
                best_value = milli;
            }
        }
        globfree(&matches);
    }

    if (best_score < 0) {
        return -1;
    }
    *temperature = best_value;
    return 0;
}

static int read_person_count(void)
{
    FILE *file = fopen(g_person_file, "r");
    if (file == NULL) {
        return 0;
    }
    long value = 0;
    int result = fscanf(file, "%ld", &value);
    fclose(file);
    if (result != 1 || value < 0 || value > 10000) {
        return 0;
    }
    return (int)value;
}

static void update_telemetry_sample(void)
{
    uint64_t total = 0, idle = 0;
    unsigned long long memory_total = 0, memory_free = 0, memory_available = 0;
    double cpu_usage = 0.0;

    pthread_mutex_lock(&g_telemetry.mutex);
    if (read_cpu_counters(&total, &idle) == 0) {
        if (g_telemetry.initialized) {
            uint64_t total_delta = total - g_telemetry.previous_total;
            uint64_t idle_delta = idle - g_telemetry.previous_idle;
            if (total_delta > 0U && idle_delta <= total_delta) {
                cpu_usage = 100.0 * (double)(total_delta - idle_delta) / (double)total_delta;
                if (cpu_usage < 0.0) cpu_usage = 0.0;
                if (cpu_usage > 100.0) cpu_usage = 100.0;
            }
        }
        g_telemetry.previous_total = total;
        g_telemetry.previous_idle = idle;
        g_telemetry.initialized = true;
        g_telemetry.cpu_usage_percent = cpu_usage;
    }

    if (read_memory_kb(&memory_total, &memory_free, &memory_available) == 0) {
        g_telemetry.memory_total_kb = memory_total;
        g_telemetry.memory_free_kb = memory_free;
        g_telemetry.memory_available_kb = memory_available;
    }

    long long local_milli = 0;
    if (find_local_cpu_temperature_millidegrees(&local_milli) == 0) {
        g_telemetry.temperature_available = true;
        g_telemetry.temperature_stale = false;
        g_telemetry.temperature_c = (double)local_milli / 1000.0;
        snprintf(g_telemetry.temperature_source, sizeof(g_telemetry.temperature_source), "%s", "local_sysfs");
    } else {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        pthread_mutex_lock(&g_host_temperature.mutex);
        if (g_host_temperature.valid) {
            double age = monotonic_age_seconds(now, g_host_temperature.received_mono);
            g_telemetry.temperature_available = age <= TEMP_STALE_SECONDS;
            g_telemetry.temperature_stale = age > TEMP_STALE_SECONDS;
            g_telemetry.temperature_c = g_host_temperature.celsius;
            snprintf(g_telemetry.temperature_source, sizeof(g_telemetry.temperature_source), "%s", "host_sysfs_udp");
        } else {
            g_telemetry.temperature_available = false;
            g_telemetry.temperature_stale = true;
            g_telemetry.temperature_c = 0.0;
            snprintf(g_telemetry.temperature_source, sizeof(g_telemetry.temperature_source), "%s", "unavailable");
        }
        pthread_mutex_unlock(&g_host_temperature.mutex);
    }
    format_timestamp(g_telemetry.sampled_at, sizeof(g_telemetry.sampled_at));
    pthread_mutex_unlock(&g_telemetry.mutex);
}

static void *telemetry_sampler_thread(void *unused)
{
    (void)unused;
    while (!g_stop) {
        update_telemetry_sample();
        for (int tick = 0; tick < 10 && !g_stop; ++tick) {
            struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000L};
            nanosleep(&delay, NULL);
        }
    }
    return NULL;
}

static void history_add(int persons)
{
    history_record_t record;
    memset(&record, 0, sizeof(record));
    record.persons = persons;
    format_timestamp(record.timestamp, sizeof(record.timestamp));

    pthread_mutex_lock(&g_history.mutex);
    record.id = g_history.next_id++;
    g_history.records[g_history.next] = record;
    g_history.next = (g_history.next + 1U) % HISTORY_CAPACITY;
    if (g_history.count < HISTORY_CAPACITY) {
        ++g_history.count;
    }
    pthread_mutex_unlock(&g_history.mutex);

    fprintf(stderr, "Detection history event: id=%llu persons=%d timestamp=%s\n",
            (unsigned long long)record.id, persons, record.timestamp);
}

static void history_clear(void)
{
    pthread_mutex_lock(&g_history.mutex);
    memset(g_history.records, 0, sizeof(g_history.records));
    g_history.count = 0;
    g_history.next = 0;
    pthread_mutex_unlock(&g_history.mutex);
}

static void *history_monitor_thread(void *unused)
{
    (void)unused;
    int previous = 0;
    time_t last_recorded = 0;
    while (!g_stop) {
        int persons = read_person_count();
        time_t now = time(NULL);
        if (persons > 0 &&
            (previous == 0 || persons != previous || now - last_recorded >= HISTORY_RECORD_INTERVAL_SECONDS)) {
            history_add(persons);
            last_recorded = now;
        }
        previous = persons;
        for (int tick = 0; tick < 5 && !g_stop; ++tick) {
            struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000L};
            nanosleep(&delay, NULL);
        }
    }
    return NULL;
}

static int write_latest_frame_file(const unsigned char *data, size_t size)
{
    char temporary[640];
    snprintf(temporary, sizeof(temporary), "%s.tmp", g_latest_frame_file);
    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return -1;
    }
    size_t offset = 0;
    while (offset < size) {
        ssize_t written = write(fd, data + offset, size - offset);
        if (written < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(temporary);
            return -1;
        }
        offset += (size_t)written;
    }
    if (fsync(fd) != 0) {
        close(fd);
        unlink(temporary);
        return -1;
    }
    close(fd);
    if (rename(temporary, g_latest_frame_file) != 0) {
        unlink(temporary);
        return -1;
    }
    return 0;
}

static void replace_frame(unsigned char *data, size_t size)
{
    static bool have_last_file_write = false;
    static struct timespec last_file_write;

    struct timespec received_now;
    clock_gettime(CLOCK_MONOTONIC, &received_now);

    pthread_mutex_lock(&g_frame.mutex);
    unsigned char *old = g_frame.data;
    g_frame.data = data;
    g_frame.size = size;
    ++g_frame.sequence;
    g_frame.received_mono = received_now;
    pthread_cond_broadcast(&g_frame.cond);
    pthread_mutex_unlock(&g_frame.mutex);
    free(old);

    /* /run is normally tmpfs. Limit snapshot writes to 5 FPS so the vision
       hand-off file cannot dominate CPU or I/O during a high-FPS stream. */
    double since_last_write = have_last_file_write
        ? monotonic_age_seconds(received_now, last_file_write)
        : 1.0;
    if (since_last_write >= 0.2) {
        if (write_latest_frame_file(data, size) != 0) {
            static time_t last_warning = 0;
            time_t now = time(NULL);
            if (now != last_warning) {
                fprintf(stderr, "Warning: cannot update %s: %s\n", g_latest_frame_file, strerror(errno));
                last_warning = now;
            }
        } else {
            last_file_write = received_now;
            have_last_file_write = true;
        }
    }
}

static int recv_all(int fd, void *buffer, size_t size)
{
    unsigned char *cursor = buffer;
    size_t remaining = size;
    while (remaining > 0U && !g_stop) {
        ssize_t received = recv(fd, cursor, remaining, 0);
        if (received == 0) return -1;
        if (received < 0) {
            if (errno == EINTR) continue;

            /*
             * SO_RCVTIMEO reports an expired receive timeout as
             * EAGAIN/EWOULDBLOCK. Return to the camera receiver so it can
             * close the stale connection and accept the host reconnect.
             */
            return -1;
        }
        cursor += (size_t)received;
        remaining -= (size_t)received;
    }
    return remaining == 0U ? 0 : -1;
}

static void *temperature_receiver_thread(void *unused)
{
    (void)unused;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        fprintf(stderr, "Temperature socket error: %s\n", strerror(errno));
        return NULL;
    }
    g_temp_socket = fd;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)g_temp_port);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        fprintf(stderr, "Cannot bind UDP temperature port %d: %s\n", g_temp_port, strerror(errno));
        close(fd);
        g_temp_socket = -1;
        return NULL;
    }
    fprintf(stderr, "Temperature receiver listening on UDP %d\n", g_temp_port);

    while (!g_stop) {
        struct pollfd poll_fd = {.fd = fd, .events = POLLIN, .revents = 0};
        int ready = poll(&poll_fd, 1, 1000);
        if (ready <= 0) {
            if (ready < 0 && errno != EINTR) fprintf(stderr, "Temperature poll error: %s\n", strerror(errno));
            continue;
        }
        char packet[256];
        struct sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);
        ssize_t count = recvfrom(fd, packet, sizeof(packet) - 1U, 0, (struct sockaddr *)&peer, &peer_length);
        if (count <= 0) continue;
        packet[count] = '\0';
        if (!peer_is_allowed(&peer)) continue;

        long long epoch = 0, milli_celsius = 0;
        if (sscanf(packet, "SGTEMP1 %lld %lld", &epoch, &milli_celsius) != 2) continue;
        (void)epoch;
        if (milli_celsius < -50000LL || milli_celsius > 200000LL) continue;

        pthread_mutex_lock(&g_host_temperature.mutex);
        g_host_temperature.valid = true;
        g_host_temperature.celsius = (double)milli_celsius / 1000.0;
        clock_gettime(CLOCK_MONOTONIC, &g_host_temperature.received_mono);
        pthread_mutex_unlock(&g_host_temperature.mutex);
    }
    if (fd >= 0) close(fd);
    g_temp_socket = -1;
    return NULL;
}

static void *camera_receiver_thread(void *unused)
{
    (void)unused;
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        fprintf(stderr, "Camera socket error: %s\n", strerror(errno));
        return NULL;
    }
    g_camera_listen_socket = listen_fd;
    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)g_camera_port);
    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(listen_fd, 4) != 0) {
        fprintf(stderr, "Cannot bind/listen TCP camera port %d: %s\n", g_camera_port, strerror(errno));
        close(listen_fd);
        g_camera_listen_socket = -1;
        return NULL;
    }
    fprintf(stderr, "Camera receiver listening on TCP %d\n", g_camera_port);

    while (!g_stop) {
        struct pollfd poll_fd = {.fd = listen_fd, .events = POLLIN, .revents = 0};
        int ready = poll(&poll_fd, 1, 1000);
        if (ready <= 0) {
            if (ready < 0 && errno != EINTR) fprintf(stderr, "Camera accept poll error: %s\n", strerror(errno));
            continue;
        }
        struct sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);
        int client = accept(listen_fd, (struct sockaddr *)&peer, &peer_length);
        if (client < 0) continue;
        if (!peer_is_allowed(&peer)) {
            close(client);
            continue;
        }
        struct timeval timeout = {.tv_sec = 3, .tv_usec = 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        pthread_mutex_lock(&g_frame.mutex);
        g_frame.camera_connected = true;
        pthread_mutex_unlock(&g_frame.mutex);
        fprintf(stderr, "Host camera connected\n");

        while (!g_stop) {
            uint32_t network_length = 0;
            if (recv_all(client, &network_length, sizeof(network_length)) != 0) break;
            uint32_t frame_length = ntohl(network_length);
            if (frame_length < 4U || frame_length > MAX_FRAME_SIZE) break;
            unsigned char *frame = malloc((size_t)frame_length);
            if (frame == NULL) break;
            if (recv_all(client, frame, (size_t)frame_length) != 0) {
                free(frame);
                break;
            }
            if (frame[0] != 0xFFU || frame[1] != 0xD8U ||
                frame[frame_length - 2U] != 0xFFU || frame[frame_length - 1U] != 0xD9U) {
                free(frame);
                continue;
            }
            replace_frame(frame, (size_t)frame_length);
        }
        close(client);
        pthread_mutex_lock(&g_frame.mutex);
        g_frame.camera_connected = false;
        pthread_mutex_unlock(&g_frame.mutex);
        fprintf(stderr, "Host camera disconnected; waiting for automatic reconnect\n");
    }
    if (listen_fd >= 0) close(listen_fd);
    g_camera_listen_socket = -1;
    return NULL;
}

static enum MHD_Result queue_buffer(
    struct MHD_Connection *connection,
    unsigned int status,
    const void *data,
    size_t size,
    const char *content_type
)
{
    struct MHD_Response *response = MHD_create_response_from_buffer(size, (void *)data, MHD_RESPMEM_MUST_COPY);
    if (response == NULL) return MHD_NO;
    if (content_type != NULL) MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, content_type);
    MHD_add_response_header(response, "Cache-Control", "no-store");
    MHD_add_response_header(response, "X-Content-Type-Options", "nosniff");
    enum MHD_Result result = MHD_queue_response(connection, status, response);
    MHD_destroy_response(response);
    return result;
}

static enum MHD_Result queue_text(
    struct MHD_Connection *connection,
    unsigned int status,
    const char *text,
    const char *content_type
)
{
    return queue_buffer(connection, status, text, strlen(text), content_type);
}

static bool path_is(const char *url, const char *expected)
{
    size_t url_length = strlen(url);
    while (url_length > 1U && url[url_length - 1U] == '/') --url_length;
    size_t expected_length = strlen(expected);
    while (expected_length > 1U && expected[expected_length - 1U] == '/') --expected_length;
    return url_length == expected_length && strncasecmp(url, expected, url_length) == 0;
}

static enum MHD_Result serve_telemetry(struct MHD_Connection *connection)
{
    double cpu = 0.0, temperature = 0.0;
    unsigned long long memory_total = 0, memory_free = 0, memory_available = 0;
    bool temp_available = false, temp_stale = true;
    char temp_source[48], timestamp[64];

    pthread_mutex_lock(&g_telemetry.mutex);
    cpu = g_telemetry.cpu_usage_percent;
    memory_total = g_telemetry.memory_total_kb;
    memory_free = g_telemetry.memory_free_kb;
    memory_available = g_telemetry.memory_available_kb;
    temp_available = g_telemetry.temperature_available;
    temp_stale = g_telemetry.temperature_stale;
    temperature = g_telemetry.temperature_c;
    snprintf(temp_source, sizeof(temp_source), "%s", g_telemetry.temperature_source);
    snprintf(timestamp, sizeof(timestamp), "%s", g_telemetry.sampled_at);
    pthread_mutex_unlock(&g_telemetry.mutex);

    bool camera_connected = false;
    double frame_age = -1.0;
    pthread_mutex_lock(&g_frame.mutex);
    camera_connected = g_frame.camera_connected;
    if (g_frame.sequence > 0U) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        frame_age = monotonic_age_seconds(now, g_frame.received_mono);
    }
    pthread_mutex_unlock(&g_frame.mutex);

    int persons = read_person_count();
    char json[2048];
    if (temp_available) {
        snprintf(json, sizeof(json),
            "{\"student_name\":\"%s\",\"student_id\":\"%s\",\"timestamp\":\"%s\","
            "\"cpu_usage_percent\":%.3f,\"memory_total_kb\":%llu,\"memory_free_kb\":%llu,"
            "\"memory_available_kb\":%llu,\"cpu_temperature_available\":true,"
            "\"cpu_temperature_stale\":%s,\"cpu_temperature_c\":%.3f,"
            "\"temperature_source\":\"%s\",\"persons\":%d,\"camera_connected\":%s,"
            "\"last_frame_age_seconds\":%.3f}",
            STUDENT_NAME, STUDENT_ID, timestamp, cpu, memory_total, memory_free,
            memory_available, temp_stale ? "true" : "false", temperature, temp_source,
            persons, camera_connected ? "true" : "false", frame_age);
    } else {
        snprintf(json, sizeof(json),
            "{\"student_name\":\"%s\",\"student_id\":\"%s\",\"timestamp\":\"%s\","
            "\"cpu_usage_percent\":%.3f,\"memory_total_kb\":%llu,\"memory_free_kb\":%llu,"
            "\"memory_available_kb\":%llu,\"cpu_temperature_available\":false,"
            "\"cpu_temperature_stale\":true,\"cpu_temperature_c\":null,"
            "\"temperature_source\":\"%s\",\"persons\":%d,\"camera_connected\":%s,"
            "\"last_frame_age_seconds\":%.3f}",
            STUDENT_NAME, STUDENT_ID, timestamp, cpu, memory_total, memory_free,
            memory_available, temp_source, persons, camera_connected ? "true" : "false", frame_age);
    }
    return queue_text(connection, MHD_HTTP_OK, json, "application/json; charset=utf-8");
}

static enum MHD_Result serve_persons(struct MHD_Connection *connection)
{
    int persons = read_person_count();
    char timestamp[64], json[512];
    format_timestamp(timestamp, sizeof(timestamp));
    snprintf(json, sizeof(json),
             "{\"student_id\":\"%s\",\"timestamp\":\"%s\",\"persons\":%d}",
             STUDENT_ID, timestamp, persons);
    return queue_text(connection, MHD_HTTP_OK, json, "application/json; charset=utf-8");
}

static enum MHD_Result serve_history(struct MHD_Connection *connection)
{
    history_record_t snapshot[HISTORY_CAPACITY];
    size_t count = 0, next = 0;
    pthread_mutex_lock(&g_history.mutex);
    count = g_history.count;
    next = g_history.next;
    memcpy(snapshot, g_history.records, sizeof(snapshot));
    pthread_mutex_unlock(&g_history.mutex);

    char json[2048];
    size_t offset = 0;
    int written = snprintf(json + offset, sizeof(json) - offset,
                           "{\"student_id\":\"%s\",\"count\":%zu,\"records\":[",
                           STUDENT_ID, count);
    if (written < 0) return MHD_NO;
    offset += (size_t)written;

    size_t start = (count == HISTORY_CAPACITY) ? next : 0U;
    for (size_t i = 0; i < count; ++i) {
        size_t index = (start + i) % HISTORY_CAPACITY;
        written = snprintf(json + offset, sizeof(json) - offset,
                           "%s{\"id\":%llu,\"timestamp\":\"%s\",\"persons\":%d}",
                           i == 0U ? "" : ",",
                           (unsigned long long)snapshot[index].id,
                           snapshot[index].timestamp,
                           snapshot[index].persons);
        if (written < 0 || (size_t)written >= sizeof(json) - offset) return MHD_NO;
        offset += (size_t)written;
    }
    written = snprintf(json + offset, sizeof(json) - offset, "]}");
    if (written < 0) return MHD_NO;
    return queue_text(connection, MHD_HTTP_OK, json, "application/json; charset=utf-8");
}

static int build_stream_chunk(stream_context_t *context)
{
    unsigned char *frame_copy = NULL;
    size_t frame_size = 0;
    uint64_t sequence = 0;

    pthread_mutex_lock(&g_frame.mutex);
    while (!g_stop && g_frame.sequence == context->last_sequence) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += 2;
        int wait_result = pthread_cond_timedwait(&g_frame.cond, &g_frame.mutex, &deadline);
        if (wait_result == ETIMEDOUT) break;
    }
    if (g_stop || g_frame.data == NULL || g_frame.size == 0U) {
        pthread_mutex_unlock(&g_frame.mutex);
        return -1;
    }
    frame_size = g_frame.size;
    sequence = g_frame.sequence;
    frame_copy = malloc(frame_size);
    if (frame_copy == NULL) {
        pthread_mutex_unlock(&g_frame.mutex);
        return -1;
    }
    memcpy(frame_copy, g_frame.data, frame_size);
    pthread_mutex_unlock(&g_frame.mutex);

    char header[256];
    int header_length = snprintf(header, sizeof(header),
        "--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\nX-Frame-Sequence: %llu\r\n\r\n",
        MJPEG_BOUNDARY, frame_size, (unsigned long long)sequence);
    if (header_length < 0 || (size_t)header_length >= sizeof(header)) {
        free(frame_copy);
        return -1;
    }

    size_t chunk_size = (size_t)header_length + frame_size + 2U;
    unsigned char *chunk = malloc(chunk_size);
    if (chunk == NULL) {
        free(frame_copy);
        return -1;
    }
    memcpy(chunk, header, (size_t)header_length);
    memcpy(chunk + (size_t)header_length, frame_copy, frame_size);
    chunk[chunk_size - 2U] = '\r';
    chunk[chunk_size - 1U] = '\n';
    free(frame_copy);

    free(context->chunk);
    context->chunk = chunk;
    context->chunk_size = chunk_size;
    context->chunk_offset = 0;
    context->last_sequence = sequence;
    return 0;
}

static ssize_t stream_reader(void *cls, uint64_t pos, char *buffer, size_t max)
{
    (void)pos;
    stream_context_t *context = cls;
    if (context == NULL || buffer == NULL || max == 0U) {
        return MHD_CONTENT_READER_END_WITH_ERROR;
    }
    if (context->chunk_offset >= context->chunk_size) {
        if (context->chunk_size > 0U) {
            ++context->frames_sent;
        }
        if (context->max_frames > 0U && context->frames_sent >= context->max_frames) {
            return MHD_CONTENT_READER_END_OF_STREAM;
        }
        if (build_stream_chunk(context) != 0) {
            return g_stop ? MHD_CONTENT_READER_END_OF_STREAM : MHD_CONTENT_READER_END_WITH_ERROR;
        }
    }
    size_t remaining = context->chunk_size - context->chunk_offset;
    size_t count = remaining < max ? remaining : max;
    memcpy(buffer, context->chunk + context->chunk_offset, count);
    context->chunk_offset += count;
    return (ssize_t)count;
}

static void stream_context_free(void *cls)
{
    stream_context_t *context = cls;
    if (context != NULL) {
        free(context->chunk);
        free(context);
    }
}

static enum MHD_Result serve_stream(struct MHD_Connection *connection)
{
    unsigned int max_frames = 0;
    const char *frames_value = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "frames");
    if (frames_value != NULL && frames_value[0] != '\0') {
        char *end = NULL;
        unsigned long parsed = strtoul(frames_value, &end, 10);
        if (end != frames_value && *end == '\0' && parsed <= 100UL) {
            max_frames = (unsigned int)parsed;
        }
    }

    stream_context_t *context = calloc(1U, sizeof(*context));
    if (context == NULL) return MHD_NO;
    context->max_frames = max_frames;

    struct MHD_Response *response = MHD_create_response_from_callback(
        MHD_SIZE_UNKNOWN,
        STREAM_BLOCK_SIZE,
        &stream_reader,
        context,
        &stream_context_free
    );
    if (response == NULL) {
        stream_context_free(context);
        return MHD_NO;
    }
    char content_type[128];
    snprintf(content_type, sizeof(content_type), "multipart/x-mixed-replace; boundary=%s", MJPEG_BOUNDARY);
    MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, content_type);
    MHD_add_response_header(response, "Cache-Control", "no-store, no-cache, must-revalidate");
    MHD_add_response_header(response, "Pragma", "no-cache");
    MHD_add_response_header(response, "Connection", "close");
    enum MHD_Result result = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return result;
}

static bool secure_equals(const char *left, const char *right)
{
    if (left == NULL || right == NULL) return false;
    size_t left_length = strlen(left), right_length = strlen(right);
    unsigned char difference = (unsigned char)(left_length ^ right_length);
    size_t maximum = left_length > right_length ? left_length : right_length;
    for (size_t index = 0; index < maximum; ++index) {
        unsigned char a = index < left_length ? (unsigned char)left[index] : 0U;
        unsigned char b = index < right_length ? (unsigned char)right[index] : 0U;
        difference = (unsigned char)(difference | (unsigned char)(a ^ b));
    }
    return difference == 0U;
}

static int extract_json_string(const char *json, const char *key, char *output, size_t output_size)
{
    if (json == NULL || key == NULL || output == NULL || output_size == 0U) return -1;
    char pattern[128];
    int pattern_length = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (pattern_length < 0 || (size_t)pattern_length >= sizeof(pattern)) return -1;
    const char *position = strstr(json, pattern);
    if (position == NULL) return -1;
    position += (size_t)pattern_length;
    while (isspace((unsigned char)*position)) ++position;
    if (*position != ':') return -1;
    ++position;
    while (isspace((unsigned char)*position)) ++position;
    if (*position != '"') return -1;
    ++position;

    size_t offset = 0;
    while (*position != '\0' && *position != '"') {
        if (*position == '\\') {
            ++position;
            if (*position == '\0') return -1;
        }
        if (offset + 1U >= output_size) return -1;
        output[offset++] = *position++;
    }
    if (*position != '"') return -1;
    output[offset] = '\0';
    return 0;
}

static void *delayed_reboot_thread(void *unused)
{
    (void)unused;
    struct timespec delay = {.tv_sec = 1, .tv_nsec = 0};
    nanosleep(&delay, NULL);
    sync();
    if (reboot(RB_AUTOBOOT) != 0) {
        fprintf(stderr, "reboot() failed: %s\n", strerror(errno));
    }
    return NULL;
}

static command_status_t command_ping(char *result, size_t result_size)
{
    char timestamp[64];
    format_timestamp(timestamp, sizeof(timestamp));
    snprintf(result, result_size, "{\"accepted\":true,\"cmd\":\"ping\",\"status\":\"ok\",\"timestamp\":\"%s\"}", timestamp);
    return COMMAND_OK;
}

static command_status_t command_history_clear(char *result, size_t result_size)
{
    history_clear();
    snprintf(result, result_size, "{\"accepted\":true,\"cmd\":\"history_clear\",\"status\":\"history cleared\"}");
    return COMMAND_OK;
}

static command_status_t command_reboot(char *result, size_t result_size)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, &delayed_reboot_thread, NULL) != 0) {
        snprintf(result, result_size, "{\"accepted\":false,\"cmd\":\"reboot\",\"error\":\"cannot schedule reboot\"}");
        return COMMAND_FAILED;
    }
    pthread_detach(thread);
    snprintf(result, result_size, "{\"accepted\":true,\"cmd\":\"reboot\",\"status\":\"reboot scheduled in 1 second\"}");
    return COMMAND_OK;
}

static const command_entry_t COMMANDS[] = {
    {"ping", "Return API liveness status", &command_ping},
    {"history_clear", "Clear the five-record in-memory history", &command_history_clear},
    {"reboot", "Reboot the VM/board through the reboot system call", &command_reboot}
};

static enum MHD_Result serve_command(
    struct MHD_Connection *connection,
    const char *body
)
{
    if (g_command_token[0] == '\0') {
        return queue_text(connection, MHD_HTTP_SERVICE_UNAVAILABLE,
                          "{\"error\":\"SMART_GUARD_COMMAND_TOKEN is not configured\"}",
                          "application/json; charset=utf-8");
    }
    const char *provided = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "X-Command-Token");
    if (!secure_equals(provided, g_command_token)) {
        return queue_text(connection, MHD_HTTP_UNAUTHORIZED,
                          "{\"error\":\"invalid or missing X-Command-Token\"}",
                          "application/json; charset=utf-8");
    }

    char command[128];
    if (extract_json_string(body, "cmd", command, sizeof(command)) != 0 || command[0] == '\0') {
        return queue_text(connection, MHD_HTTP_BAD_REQUEST,
                          "{\"error\":\"JSON body must contain a string field named cmd\"}",
                          "application/json; charset=utf-8");
    }

    char result[1024];
    for (size_t index = 0; index < sizeof(COMMANDS) / sizeof(COMMANDS[0]); ++index) {
        if (strcmp(command, COMMANDS[index].name) == 0) {
            command_status_t status = COMMANDS[index].handler(result, sizeof(result));
            unsigned int http_status = status == COMMAND_OK ? MHD_HTTP_ACCEPTED : MHD_HTTP_INTERNAL_SERVER_ERROR;
            return queue_text(connection, http_status, result, "application/json; charset=utf-8");
        }
    }

    char available[768];
    size_t offset = 0;
    int written = snprintf(available, sizeof(available), "{\"error\":\"unknown command\",\"cmd\":\"%s\",\"supported_commands\":[", command);
    if (written < 0) return MHD_NO;
    offset += (size_t)written;
    for (size_t index = 0; index < sizeof(COMMANDS) / sizeof(COMMANDS[0]); ++index) {
        written = snprintf(available + offset, sizeof(available) - offset,
                           "%s{\"name\":\"%s\",\"description\":\"%s\"}",
                           index == 0U ? "" : ",", COMMANDS[index].name, COMMANDS[index].description);
        if (written < 0 || (size_t)written >= sizeof(available) - offset) return MHD_NO;
        offset += (size_t)written;
    }
    snprintf(available + offset, sizeof(available) - offset, "]}");
    return queue_text(connection, MHD_HTTP_BAD_REQUEST, available, "application/json; charset=utf-8");
}

static enum MHD_Result route_request(
    struct MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *body,
    bool internal_only
)
{
    if (!internal_only && (path_is(url, "/") || path_is(url, "/index.html"))) {
        char *html = strdup(INDEX_HTML);
        if (html == NULL) return MHD_NO;
        char *placeholder = strstr(html, "HOST_PLACEHOLDER");
        if (placeholder != NULL) {
            size_t prefix = (size_t)(placeholder - html);
            char rendered[sizeof(INDEX_HTML) + 256U];
            snprintf(rendered, sizeof(rendered), "%.*s%s%s",
                     (int)prefix, html, g_public_host, placeholder + strlen("HOST_PLACEHOLDER"));
            free(html);
            return queue_text(connection, MHD_HTTP_OK, rendered, "text/html; charset=utf-8");
        }
        enum MHD_Result result = queue_text(connection, MHD_HTTP_OK, html, "text/html; charset=utf-8");
        free(html);
        return result;
    }

    if (path_is(url, "/api/v1/telemetry") || path_is(url, "/api/telemetry")) {
        if (strcmp(method, MHD_HTTP_METHOD_GET) != 0) goto method_not_allowed;
        return serve_telemetry(connection);
    }
    if (path_is(url, "/api/v1/persons")) {
        if (strcmp(method, MHD_HTTP_METHOD_GET) != 0) goto method_not_allowed;
        return serve_persons(connection);
    }
    if (path_is(url, "/api/v1/history")) {
        if (strcmp(method, MHD_HTTP_METHOD_GET) != 0) goto method_not_allowed;
        return serve_history(connection);
    }
    if (path_is(url, "/api/v1/stream")) {
        if (strcmp(method, MHD_HTTP_METHOD_GET) != 0) goto method_not_allowed;
        return serve_stream(connection);
    }
    if (path_is(url, "/api/v1/command")) {
        if (strcmp(method, MHD_HTTP_METHOD_POST) != 0) goto method_not_allowed;
        return serve_command(connection, body != NULL ? body : "");
    }
    if (path_is(url, "/health")) {
        if (strcmp(method, MHD_HTTP_METHOD_GET) != 0) goto method_not_allowed;
        return queue_text(connection, MHD_HTTP_OK,
                          "{\"status\":\"ok\",\"component\":\"smart_guard_c_core\"}",
                          "application/json; charset=utf-8");
    }
    return queue_text(connection, MHD_HTTP_NOT_FOUND,
                      "{\"error\":\"endpoint not found\"}",
                      "application/json; charset=utf-8");

method_not_allowed:
    return queue_text(connection, MHD_HTTP_METHOD_NOT_ALLOWED,
                      "{\"error\":\"method not allowed\"}",
                      "application/json; charset=utf-8");
}

static enum MHD_Result api_handler(
    void *cls,
    struct MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *version,
    const char *upload_data,
    size_t *upload_data_size,
    void **con_cls
)
{
    (void)version;
    bool internal_only = cls != NULL;
    request_context_t *context = *con_cls;
    if (context == NULL) {
        context = calloc(1U, sizeof(*context));
        if (context == NULL) return MHD_NO;
        *con_cls = context;
        return MHD_YES;
    }
    if (context->handled) return MHD_YES;

    if (strcmp(method, MHD_HTTP_METHOD_POST) == 0 && *upload_data_size > 0U) {
        if (context->body_size + *upload_data_size > MAX_POST_BODY) {
            *upload_data_size = 0U;
            context->handled = true;
            return queue_text(connection, MHD_HTTP_PAYLOAD_TOO_LARGE,
                              "{\"error\":\"request body too large\"}",
                              "application/json; charset=utf-8");
        }
        char *new_body = realloc(context->body, context->body_size + *upload_data_size + 1U);
        if (new_body == NULL) return MHD_NO;
        context->body = new_body;
        memcpy(context->body + context->body_size, upload_data, *upload_data_size);
        context->body_size += *upload_data_size;
        context->body[context->body_size] = '\0';
        *upload_data_size = 0U;
        return MHD_YES;
    }

    context->handled = true;
    return route_request(connection, url, method, context->body, internal_only);
}

static void request_completed(
    void *cls,
    struct MHD_Connection *connection,
    void **con_cls,
    enum MHD_RequestTerminationCode toe
)
{
    (void)cls;
    (void)connection;
    (void)toe;
    request_context_t *context = *con_cls;
    if (context != NULL) {
        free(context->body);
        free(context);
        *con_cls = NULL;
    }
}

static enum MHD_Result redirect_handler(
    void *cls,
    struct MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *version,
    const char *upload_data,
    size_t *upload_data_size,
    void **con_cls
)
{
    (void)cls; (void)url; (void)method; (void)version; (void)upload_data; (void)upload_data_size; (void)con_cls;
    char location[512];
    snprintf(location, sizeof(location), "https://%s/", g_public_host);
    struct MHD_Response *response = MHD_create_response_from_buffer(0U, (void *)"", MHD_RESPMEM_PERSISTENT);
    if (response == NULL) return MHD_NO;
    MHD_add_response_header(response, MHD_HTTP_HEADER_LOCATION, location);
    MHD_add_response_header(response, "Cache-Control", "no-store");
    enum MHD_Result result = MHD_queue_response(connection, MHD_HTTP_MOVED_PERMANENTLY, response);
    MHD_destroy_response(response);
    return result;
}

static void load_placeholder_frame(void)
{
    size_t size = 0;
    char *data = read_entire_file(g_placeholder_file, &size);
    if (data == NULL || size < 4U) {
        free(data);
        return;
    }
    unsigned char *frame = (unsigned char *)data;
    if (frame[0] == 0xFFU && frame[1] == 0xD8U) {
        replace_frame(frame, size);
    } else {
        free(frame);
    }
}

int main(void)
{
    snprintf(g_allowed_host_ip, sizeof(g_allowed_host_ip), "%s",
             get_env_string("SMART_GUARD_HOST_IP", "192.168.122.1"));
    snprintf(g_public_host, sizeof(g_public_host), "%s",
             get_env_string("SMART_GUARD_PUBLIC_HOST", "192.168.122.186"));
    snprintf(g_person_file, sizeof(g_person_file), "%s",
             get_env_string("SMART_GUARD_PERSON_FILE", "/run/smart-guard/person_count"));
    snprintf(g_latest_frame_file, sizeof(g_latest_frame_file), "%s",
             get_env_string("SMART_GUARD_LATEST_FRAME_FILE", "/run/smart-guard/latest.jpg"));
    snprintf(g_placeholder_file, sizeof(g_placeholder_file), "%s",
             get_env_string("SMART_GUARD_PLACEHOLDER_FILE", "/opt/smart-guard/share/no-camera.jpg"));
    snprintf(g_command_token, sizeof(g_command_token), "%s",
             get_env_string("SMART_GUARD_COMMAND_TOKEN", ""));

    g_http_port = get_env_port("SMART_GUARD_HTTP_PORT", DEFAULT_HTTP_PORT);
    g_https_port = get_env_port("SMART_GUARD_HTTPS_PORT", DEFAULT_HTTPS_PORT);
    g_internal_api_port = get_env_port("SMART_GUARD_INTERNAL_API_PORT", DEFAULT_INTERNAL_API_PORT);
    g_temp_port = get_env_port("SMART_GUARD_TEMP_PORT", DEFAULT_TEMP_PORT);
    g_camera_port = get_env_port("SMART_GUARD_CAMERA_PORT", DEFAULT_CAMERA_PORT);

    const char *certificate_file = get_env_string("SMART_GUARD_CERT_FILE", "/etc/smart-guard/tls/server.crt");
    const char *key_file = get_env_string("SMART_GUARD_KEY_FILE", "/etc/smart-guard/tls/server.key");
    char *certificate = read_entire_file(certificate_file, NULL);
    char *private_key = read_entire_file(key_file, NULL);
    if (certificate == NULL || private_key == NULL) {
        free(certificate);
        free(private_key);
        return EXIT_FAILURE;
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal(SIGPIPE, SIG_IGN);

    load_placeholder_frame();
    update_telemetry_sample();

    pthread_t temperature_thread, camera_thread, telemetry_thread, history_thread;
    if (pthread_create(&temperature_thread, NULL, &temperature_receiver_thread, NULL) != 0 ||
        pthread_create(&camera_thread, NULL, &camera_receiver_thread, NULL) != 0 ||
        pthread_create(&telemetry_thread, NULL, &telemetry_sampler_thread, NULL) != 0 ||
        pthread_create(&history_thread, NULL, &history_monitor_thread, NULL) != 0) {
        fprintf(stderr, "Cannot create worker threads\n");
        g_stop = 1;
        free(certificate);
        free(private_key);
        return EXIT_FAILURE;
    }

    unsigned int common_flags = MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_THREAD_PER_CONNECTION | MHD_USE_ERROR_LOG;

    struct MHD_Daemon *https_daemon = MHD_start_daemon(
        common_flags | MHD_USE_SSL,
        (uint16_t)g_https_port,
        NULL, NULL,
        &api_handler, NULL,
        MHD_OPTION_NOTIFY_COMPLETED, &request_completed, NULL,
        MHD_OPTION_HTTPS_MEM_KEY, private_key,
        MHD_OPTION_HTTPS_MEM_CERT, certificate,
        MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)30,
        MHD_OPTION_END
    );
    if (https_daemon == NULL) {
        fprintf(stderr, "Cannot start HTTPS server on port %d\n", g_https_port);
        g_stop = 1;
    }

    struct MHD_Daemon *http_daemon = NULL;
    if (!g_stop) {
        http_daemon = MHD_start_daemon(
            common_flags,
            (uint16_t)g_http_port,
            NULL, NULL,
            &redirect_handler, NULL,
            MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)10,
            MHD_OPTION_END
        );
        if (http_daemon == NULL) {
            fprintf(stderr, "Cannot start HTTP redirect server on port %d\n", g_http_port);
            g_stop = 1;
        }
    }

    struct MHD_Daemon *internal_daemon = NULL;
    struct sockaddr_in loopback;
    memset(&loopback, 0, sizeof(loopback));
    loopback.sin_family = AF_INET;
    loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    loopback.sin_port = htons((uint16_t)g_internal_api_port);

    if (!g_stop) {
        internal_daemon = MHD_start_daemon(
            common_flags,
            (uint16_t)g_internal_api_port,
            NULL, NULL,
            &api_handler, (void *)1,
            MHD_OPTION_SOCK_ADDR, (struct sockaddr *)&loopback,
            MHD_OPTION_NOTIFY_COMPLETED, &request_completed, NULL,
            MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)30,
            MHD_OPTION_END
        );
        if (internal_daemon == NULL) {
            fprintf(stderr, "Cannot start internal C API on 127.0.0.1:%d\n", g_internal_api_port);
            g_stop = 1;
        }
    }

    if (!g_stop) {
        fprintf(stderr,
                "Smart Guard Section 2 started: http=%d https=%d internal_api=127.0.0.1:%d host=%s student=%s\n",
                g_http_port, g_https_port, g_internal_api_port, g_public_host, STUDENT_ID);
        fprintf(stderr, "Swagger will use C core at http://127.0.0.1:%d\n", g_internal_api_port);
    }

    while (!g_stop) pause();

    if (internal_daemon != NULL) MHD_stop_daemon(internal_daemon);
    if (http_daemon != NULL) MHD_stop_daemon(http_daemon);
    if (https_daemon != NULL) MHD_stop_daemon(https_daemon);

    pthread_cond_broadcast(&g_frame.cond);
    pthread_join(temperature_thread, NULL);
    pthread_join(camera_thread, NULL);
    pthread_join(telemetry_thread, NULL);
    pthread_join(history_thread, NULL);

    pthread_mutex_lock(&g_frame.mutex);
    free(g_frame.data);
    g_frame.data = NULL;
    g_frame.size = 0;
    pthread_mutex_unlock(&g_frame.mutex);

    free(certificate);
    free(private_key);
    fprintf(stderr, "Smart Guard Section 2 stopped cleanly\n");
    return EXIT_SUCCESS;
}
