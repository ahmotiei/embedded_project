#!/usr/bin/env bash
set -Eeuo pipefail

# ============================================================
# Smart Guard - Section 1 all-in-one installer
# Student: Amir Hossein Motiei
# Student ID: 401102553
#
# Run on VM:
#   sudo bash section1_all_in_one.sh vm
#
# Run on physical Ubuntu host:
#   sudo bash section1_all_in_one.sh host
#
# Optional overrides:
#   VM_IP=192.168.122.186 HOST_IP=192.168.122.1 \
#     sudo -E bash section1_all_in_one.sh vm
#
#   VM_IP=192.168.122.186 VIDEO_DEVICE=/dev/video0 \
#     sudo -E bash section1_all_in_one.sh host
# ============================================================

STUDENT_NAME="Amir Hossein Motiei"
STUDENT_ID="401102553"

VM_IP="${VM_IP:-192.168.122.186}"
HOST_IP="${HOST_IP:-192.168.122.1}"
VIDEO_DEVICE="${VIDEO_DEVICE:-/dev/video0}"

INSTALL_ROOT="/opt/smart-guard"
ETC_ROOT="/etc/smart-guard"
SRC_ROOT="${INSTALL_ROOT}/source"
BIN_ROOT="${INSTALL_ROOT}/bin"
SHARE_ROOT="${INSTALL_ROOT}/share"
BUILD_ROOT="${INSTALL_ROOT}/build"

MODE="${1:-}"

log() {
    printf '\033[1;34m[smart-guard]\033[0m %s\n' "$*"
}

die() {
    printf '\033[1;31m[smart-guard:error]\033[0m %s\n' "$*" >&2
    exit 1
}

require_root() {
    [[ "${EUID}" -eq 0 ]] || die "Run this installer with sudo."
}

detect_original_user() {
    if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != "root" ]]; then
        printf '%s' "${SUDO_USER}"
    else
        id -un
    fi
}

write_common_dirs() {
    install -d -m 0755 "${INSTALL_ROOT}" "${SRC_ROOT}" "${BIN_ROOT}" \
        "${SHARE_ROOT}" "${BUILD_ROOT}"
    install -d -m 0755 "${ETC_ROOT}"
}

install_placeholder() {
    base64 -d > "${SHARE_ROOT}/no-camera.jpg" <<'B64EOF'
/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAUDBAQEAwUEBAQFBQUGBwwIBwcHBw8LCwkMEQ8SEhEPERETFhwXExQaFRERGCEYGh0dHx8fExciJCIeJBweHx7/2wBDAQUFBQcGBw4ICA4eFBEUHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh7/wAARCAFoAoADASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwD5iooorqMgooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAoopVBZgqgkk4AHegBKKfNFJDK8M0bxyISrI4wVI6gg9DTKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAJrG5msr2C8t22TQSLLG3oynIP5ivcpIrG3u5tPhSL7LZXB8aopxzF5j7Yv+BQm3OK8HopNXGex6J4Q057bwnf3mnypcXuu6RDNdRzTAXcV2sryfvNwBYbVB8sLsJKlmPIx72ztbn4a2l1/ZkcHkeFWlW4haRRLONaWM7/AJtrEJJnBHHmA9Am3zSilYD1++8M+FLzxN4isbTQo7OPRdfewtYkupi17+5vWjhcu55aS1jQFdpxIRycGqunaXa6T8Uvhk//AAj6aTPeyWk95ZM8pCy/2hLGDh2LKdqIdpPBHOec+Z6ZfXWm30d7ZyCOePO0lAwIIIIKsCCCCQQQQQSDUmsare6tcRz30iMY4xFGkcSRRxoCTtVEAVRkk4AHJJ6k0WA9U0PwvoOs6zpT32hhNPvptMkXUFuZyL2a4nhW4tQzOR8okmHHzjyMljnNcp4JvbBbbxtePpEDWv8AYqlbLzZPL/4/7MKC27eQDgn5gT7VxNFFgPXIPCHh+W8tDDonnWt9JavqLieXGjW81lbzmYEN93dNNgybhiEDkkk2tb8P6bpWieJHsPC0UIn0WU28c7Tm4QR3lpmQrvKthXZvNjYxuEOAoDqfGaKLBc9R0fwjZ3TaZDD4XF5ZS2cFyuptdyp9pmMBllgCgkSEMHjEcYV8oPmGc1qWnhDwvD4sm0670EyRXWraBZJHJNNG1ot9aySTbQHJ3BgMBy+NuDnnPjVFFgPWdG8NeGtS02y1iLQZpLi6sIZV0uzjmuRzdXcLyhfPjfAEEWTvIBkPGMYq6b4c8P3BtbWy0dL1rnxBqkEUk11IXa2tYYJI4wImKuzb2+7y5O1WGQR5hRRYDsfiD4VnsfFslpo2jXq2zwWkixLDIdkksFu7x4JYgiSdF2lmI3qCSSM8dRRTEFFFFMAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKAP/Z
B64EOF
    chmod 0644 "${SHARE_ROOT}/no-camera.jpg"
}

write_vm_source() {
    cat > "${SRC_ROOT}/smart_guard_web.c" <<'CEOF'
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
CEOF

    cat > "${SRC_ROOT}/CMakeLists-vm.txt" <<'CMAKEEOF'
cmake_minimum_required(VERSION 3.16)
project(smart_guard_web VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(MICROHTTPD REQUIRED IMPORTED_TARGET libmicrohttpd)

add_executable(smart_guard_web smart_guard_web.c)

target_compile_options(
    smart_guard_web
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wformat=2
    -Wconversion
)

target_link_libraries(
    smart_guard_web
    PRIVATE
    PkgConfig::MICROHTTPD
    Threads::Threads
)
CMAKEEOF
}

write_host_source() {
    cat > "${SRC_ROOT}/smart_guard_host_agent.c" <<'CEOF'
#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <jpeglib.h>
#include <linux/videodev2.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_VM_IP "192.168.122.186"
#define DEFAULT_VIDEO_DEVICE "/dev/video0"
#define DEFAULT_TEMP_PORT 9090
#define DEFAULT_CAMERA_PORT 9100
#define CAMERA_WIDTH 640U
#define CAMERA_HEIGHT 480U
#define CAMERA_FPS 10U
#define CAMERA_BUFFER_COUNT 4U
#define MAX_FRAME_SIZE (4U * 1024U * 1024U)

typedef struct {
    void *start;
    size_t length;
} camera_buffer_t;

typedef struct {
    int fd;
    camera_buffer_t *buffers;
    unsigned int buffer_count;
    unsigned int width;
    unsigned int height;
    uint32_t pixel_format;
} camera_t;

static volatile sig_atomic_t g_stop = 0;

static char g_vm_ip[INET_ADDRSTRLEN] = DEFAULT_VM_IP;
static char g_video_device[512] = DEFAULT_VIDEO_DEVICE;
static int g_temp_port = DEFAULT_TEMP_PORT;
static int g_camera_port = DEFAULT_CAMERA_PORT;

static void on_signal(int signo)
{
    (void)signo;
    g_stop = 1;
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
        return fallback;
    }

    return (int)parsed;
}

static int xioctl(int fd, unsigned long request, void *argument)
{
    int result;

    do {
        result = ioctl(fd, request, argument);
    } while (result == -1 && errno == EINTR);

    return result;
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

static int sensor_score(const char *path)
{
    int score = 0;

    if (strstr(path, "thermal_zone") != NULL) {
        score += 20;
    }

    if (strstr(path, "hwmon") != NULL) {
        score += 10;
    }

    char related[1024];
    snprintf(related, sizeof(related), "%s", path);

    char *last_slash = strrchr(related, '/');
    if (last_slash != NULL) {
        *last_slash = '\0';
    }

    const char *metadata_files[] = {
        "type",
        "name",
        "temp1_label",
        "temp2_label",
        "temp3_label",
        "temp4_label"
    };

    for (size_t index = 0;
         index < sizeof(metadata_files) / sizeof(metadata_files[0]);
         ++index) {
        char metadata_path[1200];
        snprintf(
            metadata_path,
            sizeof(metadata_path),
            "%s/%s",
            related,
            metadata_files[index]
        );

        FILE *file = fopen(metadata_path, "r");
        if (file == NULL) {
            continue;
        }

        char text[256];
        if (fgets(text, sizeof(text), file) != NULL) {
            for (char *cursor = text; *cursor != '\0'; ++cursor) {
                if (*cursor >= 'A' && *cursor <= 'Z') {
                    *cursor = (char)(*cursor - 'A' + 'a');
                }
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

static int find_cpu_temperature_millidegrees(long long *temperature)
{
    const char *patterns[] = {
        "/sys/class/thermal/thermal_zone*/temp",
        "/sys/class/hwmon/hwmon*/temp*_input"
    };

    int best_score = -1;
    long long best_value = 0;

    for (size_t pattern_index = 0;
         pattern_index < sizeof(patterns) / sizeof(patterns[0]);
         ++pattern_index) {
        glob_t matches;
        memset(&matches, 0, sizeof(matches));

        if (glob(patterns[pattern_index], 0, NULL, &matches) != 0) {
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

static void *temperature_sender_thread(void *unused)
{
    (void)unused;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        fprintf(stderr, "Temperature sender socket error: %s\n",
                strerror(errno));
        return NULL;
    }

    struct sockaddr_in destination;
    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons((uint16_t)g_temp_port);

    if (inet_pton(AF_INET, g_vm_ip, &destination.sin_addr) != 1) {
        fprintf(stderr, "Invalid VM IP: %s\n", g_vm_ip);
        close(fd);
        return NULL;
    }

    while (!g_stop) {
        long long milli_celsius = 0;

        if (find_cpu_temperature_millidegrees(&milli_celsius) == 0) {
            char packet[128];
            long long epoch = (long long)time(NULL);

            int length = snprintf(
                packet,
                sizeof(packet),
                "SGTEMP1 %lld %lld\n",
                epoch,
                milli_celsius
            );

            if (length > 0) {
                sendto(
                    fd,
                    packet,
                    (size_t)length,
                    0,
                    (struct sockaddr *)&destination,
                    sizeof(destination)
                );
            }
        } else {
            fprintf(stderr, "No physical host CPU temperature sensor found\n");
        }

        for (int tick = 0; tick < 20 && !g_stop; ++tick) {
            struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000L};
            nanosleep(&delay, NULL);
        }
    }

    close(fd);
    return NULL;
}

static int connect_to_vm_camera(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    struct sockaddr_in destination;
    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons((uint16_t)g_camera_port);

    if (inet_pton(AF_INET, g_vm_ip, &destination.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&destination, sizeof(destination)) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int send_all(int fd, const void *buffer, size_t size)
{
    const unsigned char *cursor = buffer;
    size_t remaining = size;

    while (remaining > 0U && !g_stop) {
        ssize_t sent = send(fd, cursor, remaining, MSG_NOSIGNAL);

        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (sent == 0) {
            return -1;
        }

        cursor += (size_t)sent;
        remaining -= (size_t)sent;
    }

    return remaining == 0U ? 0 : -1;
}

static int send_jpeg_frame(int fd, const unsigned char *data, size_t size)
{
    if (size < 4U || size > MAX_FRAME_SIZE) {
        return -1;
    }

    uint32_t network_length = htonl((uint32_t)size);

    if (send_all(fd, &network_length, sizeof(network_length)) != 0 ||
        send_all(fd, data, size) != 0) {
        return -1;
    }

    return 0;
}

static unsigned char clamp_color(int value)
{
    if (value < 0) {
        return 0U;
    }
    if (value > 255) {
        return 255U;
    }
    return (unsigned char)value;
}

static int yuyv_to_jpeg(
    const unsigned char *yuyv,
    unsigned int width,
    unsigned int height,
    unsigned char **jpeg_data,
    unsigned long *jpeg_size
)
{
    struct jpeg_compress_struct compressor;
    struct jpeg_error_mgr error_manager;

    compressor.err = jpeg_std_error(&error_manager);
    jpeg_create_compress(&compressor);

    *jpeg_data = NULL;
    *jpeg_size = 0UL;

    jpeg_mem_dest(&compressor, jpeg_data, jpeg_size);

    compressor.image_width = width;
    compressor.image_height = height;
    compressor.input_components = 3;
    compressor.in_color_space = JCS_RGB;

    jpeg_set_defaults(&compressor);
    jpeg_set_quality(&compressor, 78, TRUE);
    jpeg_start_compress(&compressor, TRUE);

    unsigned char *row = malloc((size_t)width * 3U);
    if (row == NULL) {
        jpeg_destroy_compress(&compressor);
        return -1;
    }

    while (compressor.next_scanline < compressor.image_height) {
        const unsigned char *source =
            yuyv + (size_t)compressor.next_scanline * (size_t)width * 2U;

        for (unsigned int x = 0; x < width; x += 2U) {
            int y0 = source[0];
            int u = source[1] - 128;
            int y1 = source[2];
            int v = source[3] - 128;

            int r0 = y0 + (int)(1.402 * v);
            int g0 = y0 - (int)(0.344136 * u + 0.714136 * v);
            int b0 = y0 + (int)(1.772 * u);

            int r1 = y1 + (int)(1.402 * v);
            int g1 = y1 - (int)(0.344136 * u + 0.714136 * v);
            int b1 = y1 + (int)(1.772 * u);

            row[(size_t)x * 3U + 0U] = clamp_color(r0);
            row[(size_t)x * 3U + 1U] = clamp_color(g0);
            row[(size_t)x * 3U + 2U] = clamp_color(b0);

            if (x + 1U < width) {
                row[(size_t)(x + 1U) * 3U + 0U] = clamp_color(r1);
                row[(size_t)(x + 1U) * 3U + 1U] = clamp_color(g1);
                row[(size_t)(x + 1U) * 3U + 2U] = clamp_color(b1);
            }

            source += 4;
        }

        JSAMPROW row_pointer = row;
        jpeg_write_scanlines(&compressor, &row_pointer, 1U);
    }

    free(row);
    jpeg_finish_compress(&compressor);
    jpeg_destroy_compress(&compressor);

    return (*jpeg_data != NULL && *jpeg_size > 0UL) ? 0 : -1;
}

static void close_camera(camera_t *camera)
{
    if (camera == NULL) {
        return;
    }

    if (camera->fd >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(camera->fd, VIDIOC_STREAMOFF, &type);
    }

    if (camera->buffers != NULL) {
        for (unsigned int index = 0;
             index < camera->buffer_count;
             ++index) {
            if (camera->buffers[index].start != MAP_FAILED) {
                munmap(
                    camera->buffers[index].start,
                    camera->buffers[index].length
                );
            }
        }
    }

    free(camera->buffers);

    if (camera->fd >= 0) {
        close(camera->fd);
    }

    memset(camera, 0, sizeof(*camera));
    camera->fd = -1;
}

static int open_camera(camera_t *camera)
{
    memset(camera, 0, sizeof(*camera));
    camera->fd = -1;

    int fd = open(g_video_device, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "Cannot open %s: %s\n",
                g_video_device, strerror(errno));
        return -1;
    }

    struct v4l2_capability capability;
    memset(&capability, 0, sizeof(capability));

    if (xioctl(fd, VIDIOC_QUERYCAP, &capability) != 0 ||
        !(capability.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
        !(capability.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "%s is not a streaming capture device\n",
                g_video_device);
        close(fd);
        return -1;
    }

    struct v4l2_format format;
    memset(&format, 0, sizeof(format));

    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = CAMERA_WIDTH;
    format.fmt.pix.height = CAMERA_HEIGHT;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    format.fmt.pix.field = V4L2_FIELD_ANY;

    if (xioctl(fd, VIDIOC_S_FMT, &format) != 0) {
        fprintf(stderr, "VIDIOC_S_FMT failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG &&
        format.fmt.pix.pixelformat != V4L2_PIX_FMT_JPEG &&
        format.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
        fprintf(stderr,
                "Camera returned unsupported pixel format 0x%08x\n",
                format.fmt.pix.pixelformat);
        close(fd);
        return -1;
    }

    struct v4l2_streamparm stream_parameters;
    memset(&stream_parameters, 0, sizeof(stream_parameters));
    stream_parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    stream_parameters.parm.capture.timeperframe.numerator = 1U;
    stream_parameters.parm.capture.timeperframe.denominator = CAMERA_FPS;
    xioctl(fd, VIDIOC_S_PARM, &stream_parameters);

    struct v4l2_requestbuffers request;
    memset(&request, 0, sizeof(request));
    request.count = CAMERA_BUFFER_COUNT;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;

    if (xioctl(fd, VIDIOC_REQBUFS, &request) != 0 || request.count < 2U) {
        fprintf(stderr, "VIDIOC_REQBUFS failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    camera_buffer_t *buffers =
        calloc(request.count, sizeof(*buffers));

    if (buffers == NULL) {
        close(fd);
        return -1;
    }

    for (unsigned int index = 0; index < request.count; ++index) {
        struct v4l2_buffer buffer;
        memset(&buffer, 0, sizeof(buffer));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;

        if (xioctl(fd, VIDIOC_QUERYBUF, &buffer) != 0) {
            free(buffers);
            close(fd);
            return -1;
        }

        buffers[index].length = buffer.length;
        buffers[index].start = mmap(
            NULL,
            buffer.length,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd,
            buffer.m.offset
        );

        if (buffers[index].start == MAP_FAILED) {
            for (unsigned int previous = 0; previous < index; ++previous) {
                munmap(buffers[previous].start, buffers[previous].length);
            }
            free(buffers);
            close(fd);
            return -1;
        }
    }

    for (unsigned int index = 0; index < request.count; ++index) {
        struct v4l2_buffer buffer;
        memset(&buffer, 0, sizeof(buffer));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;

        if (xioctl(fd, VIDIOC_QBUF, &buffer) != 0) {
            for (unsigned int previous = 0;
                 previous < request.count;
                 ++previous) {
                munmap(buffers[previous].start, buffers[previous].length);
            }
            free(buffers);
            close(fd);
            return -1;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (xioctl(fd, VIDIOC_STREAMON, &type) != 0) {
        for (unsigned int index = 0; index < request.count; ++index) {
            munmap(buffers[index].start, buffers[index].length);
        }
        free(buffers);
        close(fd);
        return -1;
    }

    camera->fd = fd;
    camera->buffers = buffers;
    camera->buffer_count = request.count;
    camera->width = format.fmt.pix.width;
    camera->height = format.fmt.pix.height;
    camera->pixel_format = format.fmt.pix.pixelformat;

    fprintf(
        stderr,
        "Camera ready: %s %ux%u format=0x%08x\n",
        g_video_device,
        camera->width,
        camera->height,
        camera->pixel_format
    );

    return 0;
}

static int capture_and_send_frame(camera_t *camera, int connection)
{
    fd_set descriptors;
    FD_ZERO(&descriptors);
    FD_SET(camera->fd, &descriptors);

    struct timeval timeout = {.tv_sec = 2, .tv_usec = 0};

    int ready = select(
        camera->fd + 1,
        &descriptors,
        NULL,
        NULL,
        &timeout
    );

    if (ready <= 0) {
        return ready == 0 ? 0 : -1;
    }

    struct v4l2_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;

    if (xioctl(camera->fd, VIDIOC_DQBUF, &buffer) != 0) {
        if (errno == EAGAIN) {
            return 0;
        }
        return -1;
    }

    int result = 0;

    if (buffer.index >= camera->buffer_count ||
        buffer.bytesused == 0U) {
        result = -1;
    } else if (camera->pixel_format == V4L2_PIX_FMT_MJPEG ||
               camera->pixel_format == V4L2_PIX_FMT_JPEG) {
        result = send_jpeg_frame(
            connection,
            camera->buffers[buffer.index].start,
            buffer.bytesused
        );
    } else if (camera->pixel_format == V4L2_PIX_FMT_YUYV) {
        unsigned char *jpeg_data = NULL;
        unsigned long jpeg_size = 0UL;

        if (yuyv_to_jpeg(
                camera->buffers[buffer.index].start,
                camera->width,
                camera->height,
                &jpeg_data,
                &jpeg_size
            ) != 0 ||
            jpeg_size > MAX_FRAME_SIZE) {
            result = -1;
        } else {
            result = send_jpeg_frame(
                connection,
                jpeg_data,
                (size_t)jpeg_size
            );
        }

        free(jpeg_data);
    }

    if (xioctl(camera->fd, VIDIOC_QBUF, &buffer) != 0) {
        return -1;
    }

    return result;
}

static void *camera_sender_thread(void *unused)
{
    (void)unused;

    while (!g_stop) {
        camera_t camera;

        if (open_camera(&camera) != 0) {
            for (int tick = 0; tick < 50 && !g_stop; ++tick) {
                struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000L};
                nanosleep(&delay, NULL);
            }
            continue;
        }

        while (!g_stop) {
            int connection = connect_to_vm_camera();

            if (connection < 0) {
                for (int tick = 0; tick < 20 && !g_stop; ++tick) {
                    struct timespec delay = {
                        .tv_sec = 0,
                        .tv_nsec = 100000000L
                    };
                    nanosleep(&delay, NULL);
                }
                continue;
            }

            fprintf(stderr, "Connected camera stream to VM %s:%d\n",
                    g_vm_ip, g_camera_port);

            while (!g_stop) {
                int result = capture_and_send_frame(&camera, connection);
                if (result < 0) {
                    break;
                }
            }

            close(connection);
            fprintf(stderr, "Camera stream connection closed; retrying\n");
        }

        close_camera(&camera);
    }

    return NULL;
}

int main(void)
{
    snprintf(
        g_vm_ip,
        sizeof(g_vm_ip),
        "%s",
        get_env_string("SMART_GUARD_VM_IP", DEFAULT_VM_IP)
    );

    snprintf(
        g_video_device,
        sizeof(g_video_device),
        "%s",
        get_env_string(
            "SMART_GUARD_VIDEO_DEVICE",
            DEFAULT_VIDEO_DEVICE
        )
    );

    g_temp_port = get_env_port(
        "SMART_GUARD_TEMP_PORT",
        DEFAULT_TEMP_PORT
    );

    g_camera_port = get_env_port(
        "SMART_GUARD_CAMERA_PORT",
        DEFAULT_CAMERA_PORT
    );

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
            &temperature_sender_thread,
            NULL
        ) != 0 ||
        pthread_create(
            &camera_thread,
            NULL,
            &camera_sender_thread,
            NULL
        ) != 0) {
        fprintf(stderr, "Cannot create host-agent threads\n");
        return EXIT_FAILURE;
    }

    fprintf(
        stderr,
        "Host agent started: vm=%s temp_port=%d camera_port=%d video=%s\n",
        g_vm_ip,
        g_temp_port,
        g_camera_port,
        g_video_device
    );

    pthread_join(temperature_thread, NULL);
    pthread_join(camera_thread, NULL);

    fprintf(stderr, "Host agent stopped cleanly\n");
    return EXIT_SUCCESS;
}
CEOF

    cat > "${SRC_ROOT}/CMakeLists-host.txt" <<'CMAKEEOF'
cmake_minimum_required(VERSION 3.16)
project(smart_guard_host_agent VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(JPEG REQUIRED)

add_executable(smart_guard_host_agent smart_guard_host_agent.c)

target_compile_options(
    smart_guard_host_agent
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wformat=2
    -Wconversion
)

target_link_libraries(
    smart_guard_host_agent
    PRIVATE
    Threads::Threads
    JPEG::JPEG
)
CMAKEEOF
}

install_vm() {
    require_root
    log "Installing VM-side dependencies..."
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        build-essential \
        cmake \
        pkg-config \
        libmicrohttpd-dev \
        libgnutls28-dev \
        openssl \
        ca-certificates \
        curl

    write_common_dirs
    install_placeholder
    write_vm_source

    log "Building VM HTTPS web server..."
    rm -rf "${BUILD_ROOT}/vm"
    install -d -m 0755 "${BUILD_ROOT}/vm"
    cp "${SRC_ROOT}/CMakeLists-vm.txt" "${SRC_ROOT}/CMakeLists.txt"

    cmake -S "${SRC_ROOT}" -B "${BUILD_ROOT}/vm" \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "${BUILD_ROOT}/vm" --parallel

    install -m 0755 \
        "${BUILD_ROOT}/vm/smart_guard_web" \
        "${BIN_ROOT}/smart_guard_web"

    if ! id smartguard >/dev/null 2>&1; then
        useradd \
            --system \
            --home-dir /nonexistent \
            --no-create-home \
            --shell /usr/sbin/nologin \
            smartguard
    fi

    install -d -m 0750 -o root -g smartguard "${ETC_ROOT}/tls"

    if [[ ! -s "${ETC_ROOT}/tls/server.key" ||
          ! -s "${ETC_ROOT}/tls/server.crt" ]]; then
        log "Generating self-signed TLS certificate with CN=${STUDENT_ID}..."
        openssl req \
            -x509 \
            -newkey rsa:2048 \
            -sha256 \
            -nodes \
            -days 3650 \
            -keyout "${ETC_ROOT}/tls/server.key" \
            -out "${ETC_ROOT}/tls/server.crt" \
            -subj "/CN=${STUDENT_ID}" \
            -addext "subjectAltName=IP:${VM_IP},DNS:embedded-base,DNS:localhost"
    fi

    chown root:smartguard "${ETC_ROOT}/tls/server.key" \
        "${ETC_ROOT}/tls/server.crt"
    chmod 0640 "${ETC_ROOT}/tls/server.key"
    chmod 0644 "${ETC_ROOT}/tls/server.crt"

    cat > "${ETC_ROOT}/vm.env" <<EOF
SMART_GUARD_HOST_IP=${HOST_IP}
SMART_GUARD_PUBLIC_HOST=${VM_IP}
SMART_GUARD_HTTP_PORT=80
SMART_GUARD_HTTPS_PORT=443
SMART_GUARD_TEMP_PORT=9090
SMART_GUARD_CAMERA_PORT=9100
SMART_GUARD_PERSON_FILE=/run/smart-guard/person_count
SMART_GUARD_CERT_FILE=/etc/smart-guard/tls/server.crt
SMART_GUARD_KEY_FILE=/etc/smart-guard/tls/server.key
SMART_GUARD_PLACEHOLDER_FILE=/opt/smart-guard/share/no-camera.jpg
EOF

    chown root:smartguard "${ETC_ROOT}/vm.env"
    chmod 0640 "${ETC_ROOT}/vm.env"

    cat > /etc/systemd/system/smart-guard-web.service <<'EOF'
[Unit]
Description=Smart Guard HTTPS web server and host telemetry receiver
Documentation=file:/opt/smart-guard/source/smart_guard_web.c
Wants=network-online.target
After=network-online.target

[Service]
Type=simple
User=smartguard
Group=smartguard
EnvironmentFile=/etc/smart-guard/vm.env
RuntimeDirectory=smart-guard
RuntimeDirectoryMode=0755
ExecStartPre=/bin/sh -c 'printf "0\n" > /run/smart-guard/person_count'
ExecStart=/opt/smart-guard/bin/smart_guard_web
Restart=always
RestartSec=2
TimeoutStopSec=10

AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectControlGroups=true
RestrictSUIDSGID=true
LockPersonality=true

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    systemctl enable --now smart-guard-web.service

    log "VM installation completed."
    log "HTTPS dashboard: https://${VM_IP}/"
    log "HTTP redirect test: curl -I http://${VM_IP}/"
    log "Certificate CN test:"
    log "  openssl x509 -in ${ETC_ROOT}/tls/server.crt -noout -subject -ext subjectAltName"
    log "Service status:"
    systemctl --no-pager --full status smart-guard-web.service || true
}

install_host() {
    require_root

    local run_user
    run_user="$(detect_original_user)"

    [[ "${run_user}" != "root" ]] ||
        die "Run with sudo from your normal desktop user account."

    local run_group
    run_group="$(id -gn "${run_user}")"

    log "Installing physical-host dependencies..."
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        build-essential \
        cmake \
        libjpeg-dev \
        v4l-utils \
        ca-certificates

    write_common_dirs
    write_host_source

    log "Building host temperature/camera transfer agent..."
    rm -rf "${BUILD_ROOT}/host"
    install -d -m 0755 "${BUILD_ROOT}/host"
    cp "${SRC_ROOT}/CMakeLists-host.txt" "${SRC_ROOT}/CMakeLists.txt"

    cmake -S "${SRC_ROOT}" -B "${BUILD_ROOT}/host" \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "${BUILD_ROOT}/host" --parallel

    install -m 0755 \
        "${BUILD_ROOT}/host/smart_guard_host_agent" \
        "${BIN_ROOT}/smart_guard_host_agent"

    getent group video >/dev/null 2>&1 || groupadd --system video
    usermod -aG video "${run_user}"

    cat > "${ETC_ROOT}/host-agent.env" <<EOF
SMART_GUARD_VM_IP=${VM_IP}
SMART_GUARD_VIDEO_DEVICE=${VIDEO_DEVICE}
SMART_GUARD_TEMP_PORT=9090
SMART_GUARD_CAMERA_PORT=9100
EOF

    chown root:root "${ETC_ROOT}/host-agent.env"
    chmod 0644 "${ETC_ROOT}/host-agent.env"

    cat > /etc/systemd/system/smart-guard-host-agent.service <<EOF
[Unit]
Description=Transfer physical host CPU temperature and webcam frames to Smart Guard VM
Wants=network-online.target
After=network-online.target

[Service]
Type=simple
User=${run_user}
Group=${run_group}
SupplementaryGroups=video
EnvironmentFile=/etc/smart-guard/host-agent.env
ExecStart=/opt/smart-guard/bin/smart_guard_host_agent
Restart=always
RestartSec=3
TimeoutStopSec=10

NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectControlGroups=true
RestrictSUIDSGID=true
LockPersonality=true

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    systemctl enable --now smart-guard-host-agent.service

    log "Host installation completed."
    log "The host agent sends temperature by UDP and JPEG frames by TCP to ${VM_IP}."
    log "Selected video device: ${VIDEO_DEVICE}"
    log "Available video devices:"
    ls -l /dev/video* 2>/dev/null || true
    systemctl --no-pager --full status smart-guard-host-agent.service || true
}

show_status() {
    printf '\n--- VM service ---\n'
    systemctl --no-pager --full status smart-guard-web.service 2>/dev/null || true

    printf '\n--- Host service ---\n'
    systemctl --no-pager --full status smart-guard-host-agent.service 2>/dev/null || true

    printf '\n--- Listening ports ---\n'
    ss -lntup 2>/dev/null | grep -E ':(80|443|9090|9100)\b' || true
}

show_usage() {
    cat <<EOF
Usage:
  sudo bash $0 vm
  sudo bash $0 host
  sudo bash $0 status

Current defaults:
  Student:      ${STUDENT_NAME}
  Student ID:   ${STUDENT_ID}
  Host IP:      ${HOST_IP}
  VM IP:        ${VM_IP}
  Video device: ${VIDEO_DEVICE}

Examples:
  VM_IP=192.168.122.186 HOST_IP=192.168.122.1 \
    sudo -E bash $0 vm

  VM_IP=192.168.122.186 VIDEO_DEVICE=/dev/video0 \
    sudo -E bash $0 host
EOF
}

case "${MODE}" in
    vm)
        install_vm
        ;;
    host)
        install_host
        ;;
    status)
        show_status
        ;;
    *)
        show_usage
        exit 1
        ;;
esac
