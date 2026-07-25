#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define STUDENT_NAME "Amir Hossein Motiei"
#define STUDENT_ID   "401102553"

#define DEFAULT_HTTP_PORT 80
#define DEFAULT_HTTPS_PORT 443
#define DEFAULT_TEMP_PORT 9090
#define DEFAULT_CAMERA_PORT 9100

#define MAX_FRAME_SIZE (4U * 1024U * 1024U)
#define STREAM_BLOCK_SIZE (64U * 1024U)
#define TEMP_STALE_SECONDS 10.0

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned char *data;
    size_t size;
    uint64_t sequence;
} frame_store_t;

typedef struct {
    pthread_mutex_t mutex;
    bool valid;
    double celsius;
    struct timespec received_mono;
} temperature_store_t;

typedef struct {
    pthread_mutex_t mutex;
    bool initialized;
    uint64_t previous_total;
    uint64_t previous_idle;
} cpu_state_t;

typedef struct {
    uint64_t last_sequence;
    unsigned char *chunk;
    size_t chunk_size;
    size_t chunk_offset;
} stream_context_t;

static volatile sig_atomic_t g_stop = 0;
static int g_temp_socket = -1;
static int g_camera_listen_socket = -1;

static frame_store_t g_frame = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .data = NULL,
    .size = 0,
    .sequence = 0
};

static temperature_store_t g_temperature = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .valid = false,
    .celsius = 0.0
};

static cpu_state_t g_cpu = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .initialized = false,
    .previous_total = 0,
    .previous_idle = 0
};

static char g_allowed_host_ip[INET_ADDRSTRLEN] = "192.168.122.1";
static char g_public_host[256] = "192.168.122.186";
static char g_person_file[512] = "/run/smart-guard/person_count";

static int g_http_port = DEFAULT_HTTP_PORT;
static int g_https_port = DEFAULT_HTTPS_PORT;
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
".label{color:var(--muted);font-size:.9rem}.value{font-size:clamp(1.45rem,3.5vw,2.2rem);font-weight:700;margin-top:8px}.unit{font-size:.9rem;color:var(--muted)}"
".camera{margin-top:14px}.camera img{display:block;width:100%;aspect-ratio:16/9;object-fit:contain;background:#05080e;border-radius:14px;border:1px solid var(--line)}"
".footer{margin-top:16px;color:var(--muted);font-size:.9rem}.ok{color:var(--ok)}.warn{color:var(--warn)}"
"@media(max-width:850px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}}"
"@media(max-width:480px){.grid{grid-template-columns:1fr}.wrap{padding:15px}}"
"</style>"
"</head>"
"<body>"
"<main class=\"wrap\">"
"<section class=\"top\">"
"<div><h1>Smart Guard System</h1>"
"<div class=\"sub\">Student: " STUDENT_NAME " &nbsp;|&nbsp; ID: " STUDENT_ID "</div></div>"
"<div class=\"pill\">HTTPS dashboard • refresh every 2 seconds</div>"
"</section>"
"<section class=\"grid\">"
"<article class=\"card\"><div class=\"label\">CPU usage</div><div class=\"value\" id=\"cpu\">--</div></article>"
"<article class=\"card\"><div class=\"label\">Available memory</div><div class=\"value\" id=\"mem\">--</div></article>"
"<article class=\"card\"><div class=\"label\">Physical host CPU temperature</div><div class=\"value\" id=\"temp\">--</div></article>"
"<article class=\"card\"><div class=\"label\">Persons detected</div><div class=\"value\" id=\"persons\">0</div></article>"
"</section>"
"<section class=\"card camera\">"
"<div class=\"label\">Live webcam stream transferred from the physical Ubuntu host to the VM</div>"
"<img src=\"/api/v1/stream\" alt=\"Live camera stream\">"
"</section>"
"<div class=\"footer\">"
"<span id=\"status\" class=\"warn\">Waiting for telemetry…</span>"
" &nbsp; Last update: <span id=\"timestamp\">--</span>"
"</div>"
"</main>"
"<script>"
"const $=id=>document.getElementById(id);"
"const mb=kb=>(kb/1024).toFixed(1)+' MB';"
"async function update(){"
"try{"
"const r=await fetch('/api/v1/telemetry',{cache:'no-store'});"
"if(!r.ok)throw new Error('HTTP '+r.status);"
"const d=await r.json();"
"$('cpu').textContent=d.cpu_usage_percent.toFixed(1)+' %';"
"$('mem').textContent=mb(d.memory_available_kb);"
"$('persons').textContent=d.persons;"
"if(d.cpu_temperature_available){"
"$('temp').textContent=d.cpu_temperature_c.toFixed(1)+' °C';"
"}else{$('temp').textContent='Unavailable';}"
"$('timestamp').textContent=d.timestamp;"
"$('status').textContent=d.cpu_temperature_stale?'Temperature link is stale':'System online';"
"$('status').className=d.cpu_temperature_stale?'warn':'ok';"
"}catch(e){$('status').textContent='Telemetry error: '+e.message;$('status').className='warn';}"
"}"
"update();setInterval(update,2000);"
"</script>"
"</body></html>";

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

    unsigned long long user = 0;
    unsigned long long nice = 0;
    unsigned long long system = 0;
    unsigned long long idle_ticks = 0;
    unsigned long long iowait = 0;
    unsigned long long irq = 0;
    unsigned long long softirq = 0;
    unsigned long long steal = 0;

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
    *total = (uint64_t)(
        user + nice + system + idle_ticks +
        iowait + irq + softirq + steal
    );

    return 0;
}

static double read_cpu_usage_percent(void)
{
    pthread_mutex_lock(&g_cpu.mutex);

    uint64_t total = 0;
    uint64_t idle = 0;

    if (read_cpu_counters(&total, &idle) != 0) {
        pthread_mutex_unlock(&g_cpu.mutex);
        return 0.0;
    }

    if (!g_cpu.initialized) {
        g_cpu.previous_total = total;
        g_cpu.previous_idle = idle;
        g_cpu.initialized = true;
        pthread_mutex_unlock(&g_cpu.mutex);

        struct timespec delay = {.tv_sec = 0, .tv_nsec = 150000000L};
        nanosleep(&delay, NULL);

        pthread_mutex_lock(&g_cpu.mutex);
        if (read_cpu_counters(&total, &idle) != 0) {
            pthread_mutex_unlock(&g_cpu.mutex);
            return 0.0;
        }
    }

    uint64_t total_delta = total - g_cpu.previous_total;
    uint64_t idle_delta = idle - g_cpu.previous_idle;

    g_cpu.previous_total = total;
    g_cpu.previous_idle = idle;

    pthread_mutex_unlock(&g_cpu.mutex);

    if (total_delta == 0U || idle_delta > total_delta) {
        return 0.0;
    }

    double usage = 100.0 * (double)(total_delta - idle_delta) / (double)total_delta;

    if (usage < 0.0) {
        usage = 0.0;
    } else if (usage > 100.0) {
        usage = 100.0;
    }

    return usage;
}

static int read_memory_kb(
    unsigned long long *total_kb,
    unsigned long long *available_kb
)
{
    FILE *file = fopen("/proc/meminfo", "r");
    if (file == NULL) {
        return -1;
    }

    bool have_total = false;
    bool have_available = false;
    char line[256];

    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned long long value = 0;

        if (sscanf(line, "MemTotal: %llu kB", &value) == 1) {
            *total_kb = value;
            have_total = true;
        } else if (sscanf(line, "MemAvailable: %llu kB", &value) == 1) {
            *available_kb = value;
            have_available = true;
        }
    }

    fclose(file);
    return (have_total && have_available) ? 0 : -1;
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

static void get_temperature_snapshot(
    bool *available,
    bool *stale,
    double *celsius
)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    pthread_mutex_lock(&g_temperature.mutex);

    if (!g_temperature.valid) {
        *available = false;
        *stale = true;
        *celsius = 0.0;
    } else {
        double age = monotonic_age_seconds(now, g_temperature.received_mono);
        *available = age <= TEMP_STALE_SECONDS;
        *stale = age > TEMP_STALE_SECONDS;
        *celsius = g_temperature.celsius;
    }

    pthread_mutex_unlock(&g_temperature.mutex);
}

static void format_timestamp(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm local_tm;
    localtime_r(&now, &local_tm);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%S%z", &local_tm);
}

static enum MHD_Result queue_buffer(
    struct MHD_Connection *connection,
    unsigned int status,
    const void *data,
    size_t size,
    const char *content_type
)
{
    struct MHD_Response *response =
        MHD_create_response_from_buffer(size, (void *)data, MHD_RESPMEM_MUST_COPY);

    if (response == NULL) {
        return MHD_NO;
    }

    if (content_type != NULL) {
        MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, content_type);
    }

    MHD_add_response_header(response, "Cache-Control", "no-store");

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

static int recv_all(int fd, void *buffer, size_t size)
{
    unsigned char *cursor = buffer;
    size_t remaining = size;

    while (remaining > 0U && !g_stop) {
        ssize_t received = recv(fd, cursor, remaining, 0);

        if (received == 0) {
            return -1;
        }

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }

            return -1;
        }

        cursor += (size_t)received;
        remaining -= (size_t)received;
    }

    return remaining == 0U ? 0 : -1;
}

static void replace_frame(unsigned char *data, size_t size)
{
    pthread_mutex_lock(&g_frame.mutex);

    unsigned char *old = g_frame.data;
    g_frame.data = data;
    g_frame.size = size;
    ++g_frame.sequence;

    pthread_cond_broadcast(&g_frame.cond);
    pthread_mutex_unlock(&g_frame.mutex);

    free(old);
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
        fprintf(stderr, "Cannot bind UDP temperature port %d: %s\n",
                g_temp_port, strerror(errno));
        close(fd);
        g_temp_socket = -1;
        return NULL;
    }

    fprintf(stderr, "Temperature receiver listening on UDP %d\n", g_temp_port);

    while (!g_stop) {
        struct pollfd poll_fd = {.fd = fd, .events = POLLIN, .revents = 0};
        int ready = poll(&poll_fd, 1, 1000);

        if (ready <= 0) {
            if (ready < 0 && errno != EINTR) {
                fprintf(stderr, "Temperature poll error: %s\n", strerror(errno));
            }
            continue;
        }

        char packet[256];
        struct sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);

        ssize_t count = recvfrom(
            fd,
            packet,
            sizeof(packet) - 1U,
            0,
            (struct sockaddr *)&peer,
            &peer_length
        );

        if (count <= 0) {
            continue;
        }

        packet[count] = '\0';

        if (!peer_is_allowed(&peer)) {
            continue;
        }

        long long epoch = 0;
        long long milli_celsius = 0;

        if (sscanf(packet, "SGTEMP1 %lld %lld", &epoch, &milli_celsius) != 2) {
            continue;
        }

        (void)epoch;

        if (milli_celsius < -50000LL || milli_celsius > 200000LL) {
            continue;
        }

        pthread_mutex_lock(&g_temperature.mutex);
        g_temperature.valid = true;
        g_temperature.celsius = (double)milli_celsius / 1000.0;
        clock_gettime(CLOCK_MONOTONIC, &g_temperature.received_mono);
        pthread_mutex_unlock(&g_temperature.mutex);
    }

    if (fd >= 0) {
        close(fd);
    }

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

    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(listen_fd, 4) != 0) {
        fprintf(stderr, "Cannot bind/listen TCP camera port %d: %s\n",
                g_camera_port, strerror(errno));
        close(listen_fd);
        g_camera_listen_socket = -1;
        return NULL;
    }

    fprintf(stderr, "Camera receiver listening on TCP %d\n", g_camera_port);

    while (!g_stop) {
        struct pollfd poll_fd = {.fd = listen_fd, .events = POLLIN, .revents = 0};
        int ready = poll(&poll_fd, 1, 1000);

        if (ready <= 0) {
            if (ready < 0 && errno != EINTR) {
                fprintf(stderr, "Camera accept poll error: %s\n", strerror(errno));
            }
            continue;
        }

        struct sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);
        int client = accept(listen_fd, (struct sockaddr *)&peer, &peer_length);

        if (client < 0) {
            continue;
        }

        if (!peer_is_allowed(&peer)) {
            close(client);
            continue;
        }

        struct timeval timeout = {.tv_sec = 3, .tv_usec = 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        fprintf(stderr, "Host camera connected\n");

        while (!g_stop) {
            uint32_t network_length = 0;
            if (recv_all(client, &network_length, sizeof(network_length)) != 0) {
                break;
            }

            uint32_t frame_length = ntohl(network_length);
            if (frame_length < 4U || frame_length > MAX_FRAME_SIZE) {
                break;
            }

            unsigned char *frame = malloc(frame_length);
            if (frame == NULL) {
                break;
            }

            if (recv_all(client, frame, frame_length) != 0) {
                free(frame);
                break;
            }

            if (frame[0] != 0xFFU || frame[1] != 0xD8U ||
                frame[frame_length - 2U] != 0xFFU ||
                frame[frame_length - 1U] != 0xD9U) {
                free(frame);
                continue;
            }

            replace_frame(frame, frame_length);
        }

        close(client);
        fprintf(stderr, "Host camera disconnected\n");
    }

    if (listen_fd >= 0) {
        close(listen_fd);
    }

    g_camera_listen_socket = -1;
    return NULL;
}

static ssize_t stream_reader(
    void *cls,
    uint64_t position,
    char *buffer,
    size_t max
)
{
    (void)position;

    stream_context_t *context = cls;
    if (context == NULL || g_stop) {
        return MHD_CONTENT_READER_END_OF_STREAM;
    }

    if (context->chunk_offset >= context->chunk_size) {
        free(context->chunk);
        context->chunk = NULL;
        context->chunk_size = 0;
        context->chunk_offset = 0;

        pthread_mutex_lock(&g_frame.mutex);

        if (g_frame.sequence == context->last_sequence && !g_stop) {
            struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_sec += 1;
            pthread_cond_timedwait(&g_frame.cond, &g_frame.mutex, &deadline);
        }

        if (g_stop || g_frame.data == NULL || g_frame.size == 0U) {
            pthread_mutex_unlock(&g_frame.mutex);
            return g_stop ? MHD_CONTENT_READER_END_OF_STREAM : 0;
        }

        const char *header_format =
            "--frame\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %zu\r\n"
            "\r\n";

        int header_length = snprintf(NULL, 0, header_format, g_frame.size);
        if (header_length < 0) {
            pthread_mutex_unlock(&g_frame.mutex);
            return MHD_CONTENT_READER_END_WITH_ERROR;
        }

        size_t total_size = (size_t)header_length + g_frame.size + 2U;
        unsigned char *chunk = malloc(total_size);

        if (chunk == NULL) {
            pthread_mutex_unlock(&g_frame.mutex);
            return MHD_CONTENT_READER_END_WITH_ERROR;
        }

        snprintf((char *)chunk, (size_t)header_length + 1U,
                 header_format, g_frame.size);

        memcpy(chunk + (size_t)header_length, g_frame.data, g_frame.size);
        chunk[total_size - 2U] = '\r';
        chunk[total_size - 1U] = '\n';

        context->chunk = chunk;
        context->chunk_size = total_size;
        context->chunk_offset = 0;
        context->last_sequence = g_frame.sequence;

        pthread_mutex_unlock(&g_frame.mutex);
    }

    size_t remaining = context->chunk_size - context->chunk_offset;
    size_t amount = remaining < max ? remaining : max;

    memcpy(buffer, context->chunk + context->chunk_offset, amount);
    context->chunk_offset += amount;

    return (ssize_t)amount;
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
    stream_context_t *context = calloc(1U, sizeof(*context));
    if (context == NULL) {
        return MHD_NO;
    }

    struct MHD_Response *response = MHD_create_response_from_callback(
        MHD_SIZE_UNKNOWN,
        STREAM_BLOCK_SIZE,
        &stream_reader,
        context,
        &stream_context_free
    );

    if (response == NULL) {
        free(context);
        return MHD_NO;
    }

    MHD_add_response_header(
        response,
        MHD_HTTP_HEADER_CONTENT_TYPE,
        "multipart/x-mixed-replace; boundary=frame"
    );
    MHD_add_response_header(response, "Cache-Control", "no-store, no-cache");
    MHD_add_response_header(response, "Pragma", "no-cache");
    MHD_add_response_header(response, "X-Content-Type-Options", "nosniff");

    enum MHD_Result result =
        MHD_queue_response(connection, MHD_HTTP_OK, response);

    MHD_destroy_response(response);
    return result;
}

static enum MHD_Result serve_telemetry(struct MHD_Connection *connection)
{
    unsigned long long memory_total_kb = 0;
    unsigned long long memory_available_kb = 0;

    if (read_memory_kb(&memory_total_kb, &memory_available_kb) != 0) {
        return queue_text(
            connection,
            MHD_HTTP_INTERNAL_SERVER_ERROR,
            "{\"error\":\"cannot read /proc/meminfo\"}",
            "application/json; charset=utf-8"
        );
    }

    double cpu_usage = read_cpu_usage_percent();

    bool temperature_available = false;
    bool temperature_stale = true;
    double temperature_c = 0.0;

    get_temperature_snapshot(
        &temperature_available,
        &temperature_stale,
        &temperature_c
    );

    int persons = read_person_count();

    char timestamp[64];
    format_timestamp(timestamp, sizeof(timestamp));

    char json[1024];

    if (temperature_available) {
        snprintf(
            json,
            sizeof(json),
            "{"
            "\"student_name\":\"%s\","
            "\"student_id\":\"%s\","
            "\"timestamp\":\"%s\","
            "\"cpu_usage_percent\":%.3f,"
            "\"memory_total_kb\":%llu,"
            "\"memory_available_kb\":%llu,"
            "\"cpu_temperature_available\":true,"
            "\"cpu_temperature_stale\":false,"
            "\"cpu_temperature_c\":%.3f,"
            "\"persons\":%d"
            "}",
            STUDENT_NAME,
            STUDENT_ID,
            timestamp,
            cpu_usage,
            memory_total_kb,
            memory_available_kb,
            temperature_c,
            persons
        );
    } else {
        snprintf(
            json,
            sizeof(json),
            "{"
            "\"student_name\":\"%s\","
            "\"student_id\":\"%s\","
            "\"timestamp\":\"%s\","
            "\"cpu_usage_percent\":%.3f,"
            "\"memory_total_kb\":%llu,"
            "\"memory_available_kb\":%llu,"
            "\"cpu_temperature_available\":false,"
            "\"cpu_temperature_stale\":%s,"
            "\"cpu_temperature_c\":null,"
            "\"persons\":%d"
            "}",
            STUDENT_NAME,
            STUDENT_ID,
            timestamp,
            cpu_usage,
            memory_total_kb,
            memory_available_kb,
            temperature_stale ? "true" : "false",
            persons
        );
    }

    return queue_text(
        connection,
        MHD_HTTP_OK,
        json,
        "application/json; charset=utf-8"
    );
}

static enum MHD_Result https_handler(
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
    (void)cls;
    (void)version;
    (void)upload_data;
    (void)upload_data_size;
    (void)con_cls;

    if (strcmp(method, MHD_HTTP_METHOD_GET) != 0) {
        return queue_text(
            connection,
            MHD_HTTP_METHOD_NOT_ALLOWED,
            "Method not allowed\n",
            "text/plain; charset=utf-8"
        );
    }

    if (strcmp(url, "/") == 0 || strcmp(url, "/index.html") == 0) {
        return queue_buffer(
            connection,
            MHD_HTTP_OK,
            INDEX_HTML,
            sizeof(INDEX_HTML) - 1U,
            "text/html; charset=utf-8"
        );
    }

    if (strcmp(url, "/api/v1/telemetry") == 0) {
        return serve_telemetry(connection);
    }

    if (strcmp(url, "/api/v1/stream") == 0) {
        return serve_stream(connection);
    }

    if (strcmp(url, "/health") == 0) {
        return queue_text(
            connection,
            MHD_HTTP_OK,
            "ok\n",
            "text/plain; charset=utf-8"
        );
    }

    return queue_text(
        connection,
        MHD_HTTP_NOT_FOUND,
        "Not found\n",
        "text/plain; charset=utf-8"
    );
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
    (void)cls;
    (void)version;
    (void)upload_data;
    (void)upload_data_size;
    (void)con_cls;

    if (strcmp(method, MHD_HTTP_METHOD_GET) != 0 &&
        strcmp(method, MHD_HTTP_METHOD_HEAD) != 0) {
        return queue_text(
            connection,
            MHD_HTTP_METHOD_NOT_ALLOWED,
            "Method not allowed\n",
            "text/plain; charset=utf-8"
        );
    }

    char location[1024];

    if (g_https_port == 443) {
        snprintf(location, sizeof(location), "https://%s%s", g_public_host, url);
    } else {
        snprintf(location, sizeof(location), "https://%s:%d%s",
                 g_public_host, g_https_port, url);
    }

    struct MHD_Response *response =
        MHD_create_response_from_buffer(0U, (void *)"", MHD_RESPMEM_PERSISTENT);

    if (response == NULL) {
        return MHD_NO;
    }

    MHD_add_response_header(response, MHD_HTTP_HEADER_LOCATION, location);
    MHD_add_response_header(response, "Cache-Control", "no-store");

    enum MHD_Result result =
        MHD_queue_response(connection, MHD_HTTP_MOVED_PERMANENTLY, response);

    MHD_destroy_response(response);
    return result;
}

int main(void)
{
    snprintf(
        g_allowed_host_ip,
        sizeof(g_allowed_host_ip),
        "%s",
        get_env_string("SMART_GUARD_HOST_IP", "192.168.122.1")
    );

    snprintf(
        g_public_host,
        sizeof(g_public_host),
        "%s",
        get_env_string("SMART_GUARD_PUBLIC_HOST", "192.168.122.186")
    );

    snprintf(
        g_person_file,
        sizeof(g_person_file),
        "%s",
        get_env_string(
            "SMART_GUARD_PERSON_FILE",
            "/run/smart-guard/person_count"
        )
    );

    g_http_port = get_env_port("SMART_GUARD_HTTP_PORT", DEFAULT_HTTP_PORT);
    g_https_port = get_env_port("SMART_GUARD_HTTPS_PORT", DEFAULT_HTTPS_PORT);
    g_temp_port = get_env_port("SMART_GUARD_TEMP_PORT", DEFAULT_TEMP_PORT);
    g_camera_port = get_env_port("SMART_GUARD_CAMERA_PORT", DEFAULT_CAMERA_PORT);

    const char *certificate_path = get_env_string(
        "SMART_GUARD_CERT_FILE",
        "/etc/smart-guard/tls/server.crt"
    );

    const char *key_path = get_env_string(
        "SMART_GUARD_KEY_FILE",
        "/etc/smart-guard/tls/server.key"
    );

    const char *placeholder_path = get_env_string(
        "SMART_GUARD_PLACEHOLDER_FILE",
        "/opt/smart-guard/share/no-camera.jpg"
    );

    if (MHD_is_feature_supported(MHD_FEATURE_SSL) != MHD_YES) {
        fprintf(stderr, "libmicrohttpd was built without HTTPS support\n");
        return EXIT_FAILURE;
    }

    char *certificate = read_entire_file(certificate_path, NULL);
    char *private_key = read_entire_file(key_path, NULL);

    if (certificate == NULL || private_key == NULL) {
        free(certificate);
        free(private_key);
        return EXIT_FAILURE;
    }

    size_t placeholder_size = 0;
    unsigned char *placeholder =
        (unsigned char *)read_entire_file(placeholder_path, &placeholder_size);

    if (placeholder == NULL || placeholder_size == 0U) {
        fprintf(stderr, "Cannot load camera placeholder\n");
        free(certificate);
        free(private_key);
        free(placeholder);
        return EXIT_FAILURE;
    }

    replace_frame(placeholder, placeholder_size);

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);

    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal(SIGPIPE, SIG_IGN);

    pthread_t temperature_thread;
    pthread_t camera_thread;

    if (pthread_create(
            &temperature_thread,
            NULL,
            &temperature_receiver_thread,
            NULL
        ) != 0 ||
        pthread_create(
            &camera_thread,
            NULL,
            &camera_receiver_thread,
            NULL
        ) != 0) {
        fprintf(stderr, "Cannot start receiver threads\n");
        free(certificate);
        free(private_key);
        return EXIT_FAILURE;
    }

    unsigned int common_flags =
        MHD_USE_INTERNAL_POLLING_THREAD |
        MHD_USE_THREAD_PER_CONNECTION |
        MHD_USE_ERROR_LOG;

    struct MHD_Daemon *https_daemon = MHD_start_daemon(
        common_flags | MHD_USE_SSL,
        (uint16_t)g_https_port,
        NULL,
        NULL,
        &https_handler,
        NULL,
        MHD_OPTION_HTTPS_MEM_KEY,
        private_key,
        MHD_OPTION_HTTPS_MEM_CERT,
        certificate,
        MHD_OPTION_END
    );

    if (https_daemon == NULL) {
        fprintf(stderr, "Cannot start HTTPS server on port %d\n", g_https_port);
        g_stop = 1;
        on_signal(SIGTERM);
        pthread_join(temperature_thread, NULL);
        pthread_join(camera_thread, NULL);
        free(certificate);
        free(private_key);
        return EXIT_FAILURE;
    }

    struct MHD_Daemon *http_daemon = MHD_start_daemon(
        common_flags,
        (uint16_t)g_http_port,
        NULL,
        NULL,
        &redirect_handler,
        NULL,
        MHD_OPTION_END
    );

    if (http_daemon == NULL) {
        fprintf(stderr, "Cannot start HTTP redirect server on port %d\n",
                g_http_port);
        MHD_stop_daemon(https_daemon);
        g_stop = 1;
        on_signal(SIGTERM);
        pthread_join(temperature_thread, NULL);
        pthread_join(camera_thread, NULL);
        free(certificate);
        free(private_key);
        return EXIT_FAILURE;
    }

    fprintf(
        stderr,
        "Smart Guard started: http=%d https=%d host=%s student=%s\n",
        g_http_port,
        g_https_port,
        g_public_host,
        STUDENT_ID
    );

    while (!g_stop) {
        pause();
    }

    MHD_stop_daemon(http_daemon);
    MHD_stop_daemon(https_daemon);

    pthread_cond_broadcast(&g_frame.cond);
    pthread_join(temperature_thread, NULL);
    pthread_join(camera_thread, NULL);

    pthread_mutex_lock(&g_frame.mutex);
    free(g_frame.data);
    g_frame.data = NULL;
    g_frame.size = 0;
    pthread_mutex_unlock(&g_frame.mutex);

    free(certificate);
    free(private_key);

    fprintf(stderr, "Smart Guard stopped cleanly\n");
    return EXIT_SUCCESS;
}
