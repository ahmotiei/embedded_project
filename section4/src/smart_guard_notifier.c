#define _POSIX_C_SOURCE 200809L

#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <mosquitto.h>
#include <sqlite3.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_BROKER_HOST "192.168.122.1"
#define DEFAULT_BROKER_PORT 1883
#define DEFAULT_STUDENT_ID "401102553"
#define DEFAULT_INTERVAL_SECONDS 2
#define DEFAULT_TELEMETRY_URL "http://127.0.0.1:18080/api/v1/telemetry"
#define DEFAULT_EVENT_FILE "/run/smart-guard/detection_event.json"
#define DEFAULT_SYSTEM_EVENT_FILE "/run/smart-guard/system_event.json"
#define DEFAULT_GUARD_STATE_FILE "/var/lib/smart-guard/guard_mode"
#define DEFAULT_DATABASE_FILE "/var/lib/smart-guard/blackbox.db"
#define DEFAULT_BLACKBOX_DIR "/var/lib/smart-guard/blackbox"
#define DEFAULT_CONTROL_FILE "/run/smart-guard/vision_control.json"
#define DEFAULT_THERMAL_STATUS_FILE "/run/smart-guard/thermal_status.json"
#define DEFAULT_THERMAL_STATE_FILE "/var/lib/smart-guard/thermal_active"
#define DEFAULT_EMAIL_STATE_FILE "/var/lib/smart-guard/last_email_epoch"
#define DEFAULT_EVENT_STATE_FILE "/var/lib/smart-guard/last_mqtt_event_id"
#define DEFAULT_EMAIL_EVENT_STATE_FILE "/var/lib/smart-guard/last_email_event_id"
#define DEFAULT_SEEN_EVENT_STATE_FILE "/var/lib/smart-guard/last_seen_event_id"
#define DEFAULT_ALARM_MQTT_STATE_FILE "/var/lib/smart-guard/last_alarm_mqtt_event_id"
#define DEFAULT_ALARM_EMAIL_STATE_FILE "/var/lib/smart-guard/last_alarm_email_event_id"
#define DEFAULT_SYSTEM_EVENT_STATE_FILE "/var/lib/smart-guard/last_system_event_id"
#define DEFAULT_EMAIL_DEBOUNCE_SECONDS 30
#define DEFAULT_EVENT_POLL_MILLISECONDS 200
#define DEFAULT_BLACKBOX_CAPACITY 1000
#define DEFAULT_THERMAL_HIGH_C 75.0
#define DEFAULT_THERMAL_HYSTERESIS_C 5.0
#define DEFAULT_THERMAL_MAX_FPS 4
#define DEFAULT_THERMAL_DETECTION_WIDTH 320
#define DEFAULT_THERMAL_OUTPUT_WIDTH 480
#define DEFAULT_NORMAL_MAX_FPS 0
#define DEFAULT_NORMAL_DETECTION_WIDTH 640
#define DEFAULT_NORMAL_OUTPUT_WIDTH 0
#define MAX_HTTP_RESPONSE (2U * 1024U * 1024U)
#define MAX_JSON_FILE (128U * 1024U)


typedef struct {
    char *data;
    size_t size;
} memory_buffer_t;

typedef struct {
    bool valid;
    bool temperature_available;
    double temperature_c;
    int persons;
    char timestamp[96];
} telemetry_summary_t;

typedef struct {
    unsigned long long event_id;
    int persons;
    double vision_fps;
    char timestamp[96];
    char snapshot_path[PATH_MAX];
} detection_event_t;

typedef struct {
    unsigned long long event_id;
    char type[64];
    char timestamp[96];
    char message[512];
    char service[128];
    double frame_age_seconds;
} system_event_t;

static volatile sig_atomic_t g_stop = 0;
static volatile sig_atomic_t g_connected = 0;
static char g_status_topic[256];
static char g_student_id[64];


static void handle_signal(int signal_number)
{
    (void)signal_number;
    g_stop = 1;
}


static const char *get_env_or_default(const char *name, const char *default_value)
{
    const char *value = getenv(name);
    return (value == NULL || value[0] == '\0') ? default_value : value;
}


static bool get_env_bool(const char *name, bool default_value)
{
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return default_value;
    }
    if (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0 ||
        strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0) {
        return true;
    }
    if (strcmp(value, "0") == 0 || strcasecmp(value, "false") == 0 ||
        strcasecmp(value, "no") == 0 || strcasecmp(value, "off") == 0) {
        return false;
    }
    fprintf(stderr, "Invalid boolean %s=%s; using default\n", name, value);
    return default_value;
}


static int get_env_int(const char *name, int default_value, int minimum, int maximum)
{
    const char *text = getenv(name);
    char *end = NULL;
    long value;

    if (text == NULL || text[0] == '\0') {
        return default_value;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < minimum || value > maximum) {
        fprintf(stderr, "Invalid integer %s=%s; using %d\n", name, text, default_value);
        return default_value;
    }
    return (int)value;
}


static double get_env_double(const char *name, double default_value, double minimum, double maximum)
{
    const char *text = getenv(name);
    char *end = NULL;
    double value;

    if (text == NULL || text[0] == '\0') {
        return default_value;
    }
    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || value < minimum || value > maximum) {
        fprintf(stderr, "Invalid floating point %s=%s; using %.3f\n", name, text, default_value);
        return default_value;
    }
    return value;
}


static void create_timestamp(char *output, size_t output_size)
{
    time_t now = time(NULL);
    struct tm local_time;
    if (localtime_r(&now, &local_time) == NULL ||
        strftime(output, output_size, "%Y-%m-%dT%H:%M:%S%z", &local_time) == 0U) {
        snprintf(output, output_size, "unknown");
    }
}


static long long monotonic_milliseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0LL;
    }
    return (long long)now.tv_sec * 1000LL + (long long)now.tv_nsec / 1000000LL;
}


static void sleep_milliseconds(int milliseconds)
{
    struct timespec request;
    request.tv_sec = milliseconds / 1000;
    request.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    while (!g_stop && nanosleep(&request, &request) != 0 && errno == EINTR) {
        /* Continue sleeping for the remaining interval. */
    }
}


static int ensure_directory(const char *path, mode_t mode)
{
    char buffer[PATH_MAX];
    size_t length;

    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    length = strlen(path);
    if (length >= sizeof(buffer)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(buffer, path, length + 1U);

    for (char *cursor = buffer + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            *cursor = '\0';
            if (mkdir(buffer, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            *cursor = '/';
        }
    }
    if (mkdir(buffer, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}


static int ensure_parent_directory(const char *path, mode_t mode)
{
    char parent[PATH_MAX];
    char *slash;
    if (snprintf(parent, sizeof(parent), "%s", path) >= (int)sizeof(parent)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    slash = strrchr(parent, '/');
    if (slash == NULL || slash == parent) {
        return 0;
    }
    *slash = '\0';
    return ensure_directory(parent, mode);
}


static char *read_entire_file(const char *path, size_t maximum_size)
{
    FILE *file = fopen(path, "rb");
    long length;
    size_t read_size;
    char *buffer;

    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0L || (unsigned long)length > maximum_size) {
        fclose(file);
        errno = EFBIG;
        return NULL;
    }
    rewind(file);

    buffer = calloc((size_t)length + 1U, 1U);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }
    read_size = fread(buffer, 1U, (size_t)length, file);
    if (read_size != (size_t)length && ferror(file) != 0) {
        free(buffer);
        fclose(file);
        return NULL;
    }
    buffer[read_size] = '\0';
    fclose(file);
    return buffer;
}


static int atomic_write_text(const char *path, const char *text, mode_t mode)
{
    char temporary[PATH_MAX];
    FILE *file;
    size_t length = strlen(text);

    if (ensure_parent_directory(path, 0750) != 0) {
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
    if (fwrite(text, 1U, length, file) != length || fflush(file) != 0 || fsync(fileno(file)) != 0) {
        int saved_errno = errno;
        fclose(file);
        unlink(temporary);
        errno = saved_errno;
        return -1;
    }
    if (fchmod(fileno(file), mode) != 0) {
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
    return 0;
}


static unsigned long long read_unsigned_state(const char *path)
{
    char *text = read_entire_file(path, 128U);
    char *end = NULL;
    unsigned long long value = 0ULL;
    if (text == NULL) {
        return 0ULL;
    }
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text) {
        value = 0ULL;
    }
    free(text);
    return value;
}


static int write_unsigned_state(const char *path, unsigned long long value)
{
    char line[64];
    snprintf(line, sizeof(line), "%llu\n", value);
    return atomic_write_text(path, line, 0640);
}


static bool read_boolean_state(const char *path, bool default_value)
{
    char *text = read_entire_file(path, 128U);
    bool result = default_value;
    if (text == NULL) {
        return default_value;
    }
    if (text[0] == '1' || strncasecmp(text, "true", 4U) == 0 || strncasecmp(text, "armed", 5U) == 0) {
        result = true;
    } else if (text[0] == '0' || strncasecmp(text, "false", 5U) == 0 ||
               strncasecmp(text, "disarmed", 8U) == 0) {
        result = false;
    }
    free(text);
    return result;
}


static int write_boolean_state(const char *path, bool value)
{
    return atomic_write_text(path, value ? "1\n" : "0\n", 0640);
}


static int copy_file_atomic(const char *source, const char *destination)
{
    int input_fd = -1;
    int output_fd = -1;
    char temporary[PATH_MAX];
    unsigned char buffer[64U * 1024U];
    int return_code = -1;

    if (ensure_parent_directory(destination, 0750) != 0) {
        return -1;
    }
    if (snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", destination, (long)getpid()) >=
        (int)sizeof(temporary)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    input_fd = open(source, O_RDONLY | O_CLOEXEC);
    if (input_fd < 0) {
        return -1;
    }
    output_fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0640);
    if (output_fd < 0) {
        goto cleanup;
    }

    for (;;) {
        ssize_t read_count = read(input_fd, buffer, sizeof(buffer));
        if (read_count == 0) {
            break;
        }
        if (read_count < 0) {
            if (errno == EINTR) {
                continue;
            }
            goto cleanup;
        }
        size_t offset = 0U;
        while (offset < (size_t)read_count) {
            ssize_t written = write(output_fd, buffer + offset, (size_t)read_count - offset);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                goto cleanup;
            }
            offset += (size_t)written;
        }
    }
    if (fsync(output_fd) != 0 || close(output_fd) != 0) {
        output_fd = -1;
        goto cleanup;
    }
    output_fd = -1;
    if (rename(temporary, destination) != 0) {
        goto cleanup;
    }
    return_code = 0;

cleanup:
    if (output_fd >= 0) {
        close(output_fd);
    }
    if (input_fd >= 0) {
        close(input_fd);
    }
    if (return_code != 0) {
        unlink(temporary);
    }
    return return_code;
}


static size_t smart_guard_http_write_callback(void *contents, size_t size, size_t count, void *user_data)
{
    size_t received = size * count;
    memory_buffer_t *buffer = user_data;
    char *new_data;

    if (received == 0U || buffer->size > MAX_HTTP_RESPONSE - received - 1U) {
        return 0U;
    }
    new_data = realloc(buffer->data, buffer->size + received + 1U);
    if (new_data == NULL) {
        return 0U;
    }
    buffer->data = new_data;
    memcpy(buffer->data + buffer->size, contents, received);
    buffer->size += received;
    buffer->data[buffer->size] = '\0';
    return received;
}


static char *fetch_telemetry_json(const char *telemetry_url)
{
    CURL *curl = NULL;
    CURLcode result;
    long response_code = 0L;
    memory_buffer_t response = {.data = calloc(1U, 1U), .size = 0U};

    if (response.data == NULL) {
        return NULL;
    }
    curl = curl_easy_init();
    if (curl == NULL) {
        free(response.data);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, telemetry_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, smart_guard_http_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 4L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "smart-guard-section4-controller/1.0");

    result = curl_easy_perform(curl);
    if (result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    }
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        fprintf(stderr, "Telemetry request failed: %s\n", curl_easy_strerror(result));
        free(response.data);
        return NULL;
    }
    if (response_code != 200L) {
        fprintf(stderr, "Telemetry endpoint returned HTTP %ld\n", response_code);
        free(response.data);
        return NULL;
    }
    return response.data;
}


static bool parse_telemetry_summary(const char *json_text, telemetry_summary_t *summary)
{
    cJSON *root = NULL;
    cJSON *persons_item;
    cJSON *timestamp_item;
    cJSON *temperature_item;
    cJSON *available_item;

    memset(summary, 0, sizeof(*summary));
    root = cJSON_Parse(json_text);
    if (root == NULL) {
        return false;
    }

    persons_item = cJSON_GetObjectItemCaseSensitive(root, "persons");
    timestamp_item = cJSON_GetObjectItemCaseSensitive(root, "timestamp");
    temperature_item = cJSON_GetObjectItemCaseSensitive(root, "cpu_temperature_c");
    available_item = cJSON_GetObjectItemCaseSensitive(root, "cpu_temperature_available");

    if (!cJSON_IsNumber(persons_item) || !cJSON_IsString(timestamp_item) ||
        timestamp_item->valuestring == NULL) {
        cJSON_Delete(root);
        return false;
    }

    summary->persons = persons_item->valueint;
    snprintf(summary->timestamp, sizeof(summary->timestamp), "%s", timestamp_item->valuestring);
    summary->temperature_available = cJSON_IsTrue(available_item) && cJSON_IsNumber(temperature_item);
    if (summary->temperature_available) {
        summary->temperature_c = temperature_item->valuedouble;
    }
    summary->valid = true;
    cJSON_Delete(root);
    return true;
}


static bool parse_detection_event(const char *json_text, detection_event_t *event)
{
    cJSON *root = cJSON_Parse(json_text);
    cJSON *event_id_item;
    cJSON *persons_item;
    cJSON *timestamp_item;
    cJSON *snapshot_item;
    cJSON *fps_item;

    memset(event, 0, sizeof(*event));
    if (root == NULL) {
        return false;
    }

    event_id_item = cJSON_GetObjectItemCaseSensitive(root, "event_id");
    persons_item = cJSON_GetObjectItemCaseSensitive(root, "persons");
    timestamp_item = cJSON_GetObjectItemCaseSensitive(root, "timestamp");
    snapshot_item = cJSON_GetObjectItemCaseSensitive(root, "snapshot_path");
    fps_item = cJSON_GetObjectItemCaseSensitive(root, "vision_fps");

    if ((!cJSON_IsString(event_id_item) && !cJSON_IsNumber(event_id_item)) ||
        !cJSON_IsNumber(persons_item) || !cJSON_IsString(timestamp_item) ||
        !cJSON_IsString(snapshot_item) || timestamp_item->valuestring == NULL ||
        snapshot_item->valuestring == NULL) {
        cJSON_Delete(root);
        return false;
    }

    if (cJSON_IsString(event_id_item) && event_id_item->valuestring != NULL) {
        char *end = NULL;
        errno = 0;
        event->event_id = strtoull(event_id_item->valuestring, &end, 10);
        if (errno != 0 || end == event_id_item->valuestring || *end != '\0') {
            cJSON_Delete(root);
            return false;
        }
    } else {
        event->event_id = (unsigned long long)event_id_item->valuedouble;
    }
    event->persons = persons_item->valueint;
    event->vision_fps = cJSON_IsNumber(fps_item) ? fps_item->valuedouble : 0.0;
    snprintf(event->timestamp, sizeof(event->timestamp), "%s", timestamp_item->valuestring);
    snprintf(event->snapshot_path, sizeof(event->snapshot_path), "%s", snapshot_item->valuestring);
    cJSON_Delete(root);
    return event->event_id > 0ULL && event->persons > 0;
}


static bool parse_system_event(const char *json_text, system_event_t *event)
{
    cJSON *root = cJSON_Parse(json_text);
    cJSON *id_item;
    cJSON *type_item;
    cJSON *timestamp_item;
    cJSON *message_item;
    cJSON *service_item;
    cJSON *age_item;

    memset(event, 0, sizeof(*event));
    if (root == NULL) {
        return false;
    }
    id_item = cJSON_GetObjectItemCaseSensitive(root, "event_id");
    type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
    timestamp_item = cJSON_GetObjectItemCaseSensitive(root, "timestamp");
    message_item = cJSON_GetObjectItemCaseSensitive(root, "message");
    service_item = cJSON_GetObjectItemCaseSensitive(root, "service");
    age_item = cJSON_GetObjectItemCaseSensitive(root, "frame_age_seconds");

    if ((!cJSON_IsString(id_item) && !cJSON_IsNumber(id_item)) ||
        !cJSON_IsString(type_item) || !cJSON_IsString(timestamp_item) ||
        type_item->valuestring == NULL || timestamp_item->valuestring == NULL) {
        cJSON_Delete(root);
        return false;
    }
    if (cJSON_IsString(id_item) && id_item->valuestring != NULL) {
        char *end = NULL;
        errno = 0;
        event->event_id = strtoull(id_item->valuestring, &end, 10);
        if (errno != 0 || end == id_item->valuestring || *end != '\0') {
            cJSON_Delete(root);
            return false;
        }
    } else {
        event->event_id = (unsigned long long)id_item->valuedouble;
    }
    snprintf(event->type, sizeof(event->type), "%s", type_item->valuestring);
    snprintf(event->timestamp, sizeof(event->timestamp), "%s", timestamp_item->valuestring);
    if (cJSON_IsString(message_item) && message_item->valuestring != NULL) {
        snprintf(event->message, sizeof(event->message), "%s", message_item->valuestring);
    }
    if (cJSON_IsString(service_item) && service_item->valuestring != NULL) {
        snprintf(event->service, sizeof(event->service), "%s", service_item->valuestring);
    }
    event->frame_age_seconds = cJSON_IsNumber(age_item) ? age_item->valuedouble : -1.0;
    cJSON_Delete(root);
    return event->event_id > 0ULL;
}


static char *build_status_payload(const char *status)
{
    cJSON *payload = cJSON_CreateObject();
    char timestamp[64];
    char *result;
    if (payload == NULL) {
        return NULL;
    }
    create_timestamp(timestamp, sizeof(timestamp));
    cJSON_AddStringToObject(payload, "student_id", g_student_id);
    cJSON_AddStringToObject(payload, "status", status);
    cJSON_AddStringToObject(payload, "timestamp", timestamp);
    result = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    return result;
}


static char *build_persons_payload(const telemetry_summary_t *telemetry, const detection_event_t *event)
{
    cJSON *payload = cJSON_CreateObject();
    char published_at[64];
    char *result;
    if (payload == NULL) {
        return NULL;
    }
    create_timestamp(published_at, sizeof(published_at));

    cJSON_AddStringToObject(payload, "student_id", g_student_id);
    cJSON_AddNumberToObject(payload, "persons", event != NULL ? event->persons : telemetry->persons);
    cJSON_AddStringToObject(payload, "timestamp", event != NULL ? event->timestamp : telemetry->timestamp);
    cJSON_AddStringToObject(payload, "published_at", published_at);
    cJSON_AddBoolToObject(payload, "temperature_available", telemetry->temperature_available);
    if (telemetry->temperature_available) {
        cJSON_AddNumberToObject(payload, "temperature_c", telemetry->temperature_c);
    } else {
        cJSON_AddNullToObject(payload, "temperature_c");
    }
    if (event != NULL) {
        char event_id_text[32];
        snprintf(event_id_text, sizeof(event_id_text), "%llu", event->event_id);
        cJSON_AddStringToObject(payload, "event", "person_detected");
        cJSON_AddStringToObject(payload, "event_id", event_id_text);
        cJSON_AddNumberToObject(payload, "vision_fps", event->vision_fps);
    } else {
        cJSON_AddStringToObject(payload, "event", "periodic_status");
    }

    result = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    return result;
}


static char *build_alarm_payload(const detection_event_t *event,
                                 const telemetry_summary_t *telemetry,
                                 const char *event_type,
                                 const char *message)
{
    cJSON *payload = cJSON_CreateObject();
    char published_at[64];
    char event_id_text[32];
    char *result;
    if (payload == NULL) {
        return NULL;
    }
    create_timestamp(published_at, sizeof(published_at));
    cJSON_AddStringToObject(payload, "student_id", g_student_id);
    cJSON_AddStringToObject(payload, "event", event_type);
    cJSON_AddStringToObject(payload, "published_at", published_at);
    if (event != NULL) {
        snprintf(event_id_text, sizeof(event_id_text), "%llu", event->event_id);
        cJSON_AddStringToObject(payload, "event_id", event_id_text);
        cJSON_AddStringToObject(payload, "timestamp", event->timestamp);
        cJSON_AddNumberToObject(payload, "persons", event->persons);
        cJSON_AddNumberToObject(payload, "vision_fps", event->vision_fps);
    }
    if (telemetry != NULL) {
        cJSON_AddBoolToObject(payload, "temperature_available", telemetry->temperature_available);
        if (telemetry->temperature_available) {
            cJSON_AddNumberToObject(payload, "temperature_c", telemetry->temperature_c);
        } else {
            cJSON_AddNullToObject(payload, "temperature_c");
        }
    }
    if (message != NULL && message[0] != '\0') {
        cJSON_AddStringToObject(payload, "message", message);
    }
    result = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    return result;
}


static int publish_qos1(struct mosquitto *mosquitto, const char *topic,
                        const char *payload, bool retain)
{
    int message_id = 0;
    int result;
    if (mosquitto == NULL || !g_connected) {
        return -1;
    }
    result = mosquitto_publish(mosquitto, &message_id, topic, (int)strlen(payload), payload, 1, retain);
    if (result != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "MQTT publish failed topic=%s: %s\n", topic, mosquitto_strerror(result));
        return -1;
    }
    fprintf(stdout, "MQTT queued QoS1 topic=%s mid=%d payload=%s\n", topic, message_id, payload);
    fflush(stdout);
    return 0;
}


static const char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}


static int send_email_message(const char *subject,
                              const char *body,
                              const char *attachment_path,
                              const char *attachment_type)
{
    const char *smtp_url = getenv("SMART_GUARD_SMTP_URL");
    const char *smtp_username = getenv("SMART_GUARD_SMTP_USERNAME");
    const char *smtp_password = getenv("SMART_GUARD_SMTP_PASSWORD");
    const char *email_from = getenv("SMART_GUARD_EMAIL_FROM");
    const char *email_to = getenv("SMART_GUARD_EMAIL_TO");
    const char *ca_file = getenv("SMART_GUARD_SMTP_CA_FILE");
    bool insecure = get_env_bool("SMART_GUARD_SMTP_INSECURE", false);
    CURL *curl = NULL;
    CURLcode result;
    struct curl_slist *recipients = NULL;
    struct curl_slist *headers = NULL;
    curl_mime *mime = NULL;
    curl_mimepart *part;
    char from_header[384];
    char to_header[384];
    char subject_header[512];
    char mail_from[384];
    struct stat attachment_stat;
    int return_code = -1;

    if (smtp_url == NULL || smtp_username == NULL || smtp_password == NULL ||
        email_from == NULL || email_to == NULL || smtp_url[0] == '\0' ||
        smtp_username[0] == '\0' || smtp_password[0] == '\0' ||
        email_from[0] == '\0' || email_to[0] == '\0' ||
        strstr(smtp_password, "REPLACE_") != NULL) {
        fprintf(stderr, "Email is enabled but SMTP/email variables are incomplete\n");
        return -1;
    }
    if (attachment_path != NULL && attachment_path[0] != '\0' &&
        (stat(attachment_path, &attachment_stat) != 0 || !S_ISREG(attachment_stat.st_mode))) {
        fprintf(stderr, "Email attachment is missing: %s\n", attachment_path);
        return -1;
    }

    snprintf(mail_from, sizeof(mail_from), "<%s>", email_from);
    snprintf(from_header, sizeof(from_header), "From: Smart Guard <%s>", email_from);
    snprintf(to_header, sizeof(to_header), "To: <%s>", email_to);
    snprintf(subject_header, sizeof(subject_header), "Subject: %s", subject);

    curl = curl_easy_init();
    if (curl == NULL) {
        return -1;
    }
    recipients = curl_slist_append(recipients, email_to);
    headers = curl_slist_append(headers, from_header);
    headers = curl_slist_append(headers, to_header);
    headers = curl_slist_append(headers, subject_header);
    headers = curl_slist_append(headers, "MIME-Version: 1.0");
    if (recipients == NULL || headers == NULL) {
        goto cleanup;
    }

    mime = curl_mime_init(curl);
    if (mime == NULL) {
        goto cleanup;
    }
    part = curl_mime_addpart(mime);
    if (part == NULL || curl_mime_data(part, body, CURL_ZERO_TERMINATED) != CURLE_OK ||
        curl_mime_type(part, "text/plain; charset=utf-8") != CURLE_OK) {
        goto cleanup;
    }
    if (attachment_path != NULL && attachment_path[0] != '\0') {
        part = curl_mime_addpart(mime);
        if (part == NULL || curl_mime_filedata(part, attachment_path) != CURLE_OK ||
            curl_mime_filename(part, path_basename(attachment_path)) != CURLE_OK ||
            curl_mime_type(part, attachment_type != NULL ? attachment_type : "application/octet-stream") != CURLE_OK ||
            curl_mime_encoder(part, "base64") != CURLE_OK) {
            goto cleanup;
        }
    }

    curl_easy_setopt(curl, CURLOPT_URL, smtp_url);
    curl_easy_setopt(curl, CURLOPT_USERNAME, smtp_username);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, smtp_password);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, mail_from);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, insecure ? 0L : 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, insecure ? 0L : 2L);
    if (ca_file != NULL && ca_file[0] != '\0') {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_file);
    }

    result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        fprintf(stderr, "Email send failed: %s\n", curl_easy_strerror(result));
        goto cleanup;
    }
    fprintf(stdout, "Email sent subject=%s to=%s\n", subject, email_to);
    fflush(stdout);
    return_code = 0;

cleanup:
    if (mime != NULL) {
        curl_mime_free(mime);
    }
    curl_slist_free_all(headers);
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);
    return return_code;
}


static int send_detection_email(const detection_event_t *event,
                                const telemetry_summary_t *telemetry,
                                bool alarm)
{
    char subject[512];
    char body[3072];
    snprintf(subject, sizeof(subject), "%sSmart Guard %s - %d person(s)",
             alarm ? "[ALARM] " : "", g_student_id, event->persons);
    if (telemetry->temperature_available) {
        snprintf(body, sizeof(body),
                 "%s\r\n\r\nStudent ID: %s\r\nPersons: %d\r\nTimestamp: %s\r\n"
                 "CPU temperature: %.3f C\r\nVision FPS: %.3f\r\nEvent ID: %llu\r\n"
                 "Guard mode at detection: %s\r\n",
                 alarm ? "URGENT: human detected while guard mode is armed." : "Smart Guard person detection",
                 g_student_id, event->persons, event->timestamp, telemetry->temperature_c,
                 event->vision_fps, event->event_id, alarm ? "ARMED" : "DISARMED");
    } else {
        snprintf(body, sizeof(body),
                 "%s\r\n\r\nStudent ID: %s\r\nPersons: %d\r\nTimestamp: %s\r\n"
                 "CPU temperature: unavailable\r\nVision FPS: %.3f\r\nEvent ID: %llu\r\n"
                 "Guard mode at detection: %s\r\n",
                 alarm ? "URGENT: human detected while guard mode is armed." : "Smart Guard person detection",
                 g_student_id, event->persons, event->timestamp, event->vision_fps,
                 event->event_id, alarm ? "ARMED" : "DISARMED");
    }
    return send_email_message(subject, body, event->snapshot_path, "image/jpeg");
}


static int sqlite_exec_checked(sqlite3 *db, const char *sql)
{
    char *error = NULL;
    int result = sqlite3_exec(db, sql, NULL, NULL, &error);
    if (result != SQLITE_OK) {
        fprintf(stderr, "SQLite error: %s; SQL=%s\n", error != NULL ? error : "unknown", sql);
        sqlite3_free(error);
        return -1;
    }
    return 0;
}


static sqlite3 *open_blackbox_database(const char *database_file)
{
    sqlite3 *db = NULL;
    if (ensure_parent_directory(database_file, 0750) != 0) {
        fprintf(stderr, "Cannot create black-box database directory: %s\n", strerror(errno));
        return NULL;
    }
    if (sqlite3_open_v2(database_file, &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        NULL) != SQLITE_OK) {
        fprintf(stderr, "Cannot open SQLite database %s: %s\n", database_file,
                db != NULL ? sqlite3_errmsg(db) : "unknown");
        if (db != NULL) {
            sqlite3_close(db);
        }
        return NULL;
    }
    sqlite3_busy_timeout(db, 3000);
    if (sqlite_exec_checked(db, "PRAGMA journal_mode=WAL;") != 0 ||
        sqlite_exec_checked(db, "PRAGMA synchronous=FULL;") != 0 ||
        sqlite_exec_checked(db,
            "CREATE TABLE IF NOT EXISTS detections("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "event_id TEXT NOT NULL UNIQUE,"
            "detected_at TEXT NOT NULL,"
            "persons INTEGER NOT NULL CHECK(persons>0),"
            "temperature_c REAL,"
            "vision_fps REAL NOT NULL,"
            "snapshot_path TEXT NOT NULL,"
            "guard_armed INTEGER NOT NULL CHECK(guard_armed IN(0,1)),"
            "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ");") != 0 ||
        sqlite_exec_checked(db,
            "CREATE TABLE IF NOT EXISTS metadata("
            "key TEXT PRIMARY KEY, value INTEGER NOT NULL"
            ");") != 0 ||
        sqlite_exec_checked(db,
            "INSERT OR IGNORE INTO metadata(key,value) VALUES"
            "('total_detection_events',0),('total_persons_detected',0);") != 0) {
        sqlite3_close(db);
        return NULL;
    }
    return db;
}


static int database_guard_for_event(sqlite3 *db, unsigned long long event_id, bool *guard_armed)
{
    sqlite3_stmt *statement = NULL;
    char event_id_text[32];
    int result = -1;
    snprintf(event_id_text, sizeof(event_id_text), "%llu", event_id);
    if (sqlite3_prepare_v2(db,
            "SELECT guard_armed FROM detections WHERE event_id=?1;", -1, &statement, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(statement, 1, event_id_text, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(statement) == SQLITE_ROW) {
        *guard_armed = sqlite3_column_int(statement, 0) != 0;
        result = 0;
    }
    sqlite3_finalize(statement);
    return result;
}


static int prune_blackbox(sqlite3 *db, int capacity)
{
    sqlite3_stmt *statement = NULL;
    char **paths = NULL;
    size_t path_count = 0U;
    size_t path_capacity = 0U;
    int result = -1;

    if (sqlite3_prepare_v2(db,
            "SELECT snapshot_path FROM detections ORDER BY id DESC LIMIT -1 OFFSET ?1;",
            -1, &statement, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int(statement, 1, capacity);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(statement, 0);
        if (text == NULL) {
            continue;
        }
        if (path_count == path_capacity) {
            size_t new_capacity = path_capacity == 0U ? 8U : path_capacity * 2U;
            char **new_paths = realloc(paths, new_capacity * sizeof(*new_paths));
            if (new_paths == NULL) {
                goto cleanup;
            }
            paths = new_paths;
            path_capacity = new_capacity;
        }
        paths[path_count] = strdup((const char *)text);
        if (paths[path_count] == NULL) {
            goto cleanup;
        }
        ++path_count;
    }
    sqlite3_finalize(statement);
    statement = NULL;

    if (sqlite3_prepare_v2(db,
            "DELETE FROM detections WHERE id NOT IN "
            "(SELECT id FROM detections ORDER BY id DESC LIMIT ?1);",
            -1, &statement, NULL) != SQLITE_OK) {
        goto cleanup;
    }
    sqlite3_bind_int(statement, 1, capacity);
    if (sqlite3_step(statement) != SQLITE_DONE) {
        goto cleanup;
    }
    result = 0;

cleanup:
    if (statement != NULL) {
        sqlite3_finalize(statement);
    }
    if (result == 0) {
        for (size_t index = 0U; index < path_count; ++index) {
            if (paths[index] != NULL && paths[index][0] != '\0') {
                unlink(paths[index]);
            }
        }
    }
    for (size_t index = 0U; index < path_count; ++index) {
        free(paths[index]);
    }
    free(paths);
    return result;
}


static int record_blackbox_event(sqlite3 *db,
                                 const detection_event_t *event,
                                 const telemetry_summary_t *telemetry,
                                 bool guard_armed,
                                 int capacity,
                                 const char *blackbox_dir,
                                 bool *stored_guard_armed)
{
    sqlite3_stmt *statement = NULL;
    char event_id_text[32];
    char persistent_snapshot[PATH_MAX];
    int inserted = 0;
    int result = -1;

    if (database_guard_for_event(db, event->event_id, stored_guard_armed) == 0) {
        return 0;
    }
    if (ensure_directory(blackbox_dir, 0750) != 0) {
        fprintf(stderr, "Cannot create black-box snapshot directory: %s\n", strerror(errno));
        return -1;
    }
    snprintf(event_id_text, sizeof(event_id_text), "%llu", event->event_id);
    if (snprintf(persistent_snapshot, sizeof(persistent_snapshot), "%s/event_%s.jpg",
                 blackbox_dir, event_id_text) >= (int)sizeof(persistent_snapshot)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (copy_file_atomic(event->snapshot_path, persistent_snapshot) != 0) {
        fprintf(stderr, "Cannot persist black-box snapshot %s: %s\n", event->snapshot_path, strerror(errno));
        return -1;
    }

    if (sqlite_exec_checked(db, "BEGIN IMMEDIATE;") != 0) {
        unlink(persistent_snapshot);
        return -1;
    }
    if (sqlite3_prepare_v2(db,
            "INSERT OR IGNORE INTO detections(event_id,detected_at,persons,temperature_c,vision_fps,snapshot_path,guard_armed)"
            " VALUES(?1,?2,?3,?4,?5,?6,?7);",
            -1, &statement, NULL) != SQLITE_OK) {
        goto rollback;
    }
    sqlite3_bind_text(statement, 1, event_id_text, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, event->timestamp, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 3, event->persons);
    if (telemetry->temperature_available) {
        sqlite3_bind_double(statement, 4, telemetry->temperature_c);
    } else {
        sqlite3_bind_null(statement, 4);
    }
    sqlite3_bind_double(statement, 5, event->vision_fps);
    sqlite3_bind_text(statement, 6, persistent_snapshot, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 7, guard_armed ? 1 : 0);
    if (sqlite3_step(statement) != SQLITE_DONE) {
        goto rollback;
    }
    inserted = sqlite3_changes(db);
    sqlite3_finalize(statement);
    statement = NULL;

    if (inserted > 0) {
        if (sqlite3_prepare_v2(db,
                "UPDATE metadata SET value=value+1 WHERE key='total_detection_events';",
                -1, &statement, NULL) != SQLITE_OK || sqlite3_step(statement) != SQLITE_DONE) {
            goto rollback;
        }
        sqlite3_finalize(statement);
        statement = NULL;
        if (sqlite3_prepare_v2(db,
                "UPDATE metadata SET value=value+?1 WHERE key='total_persons_detected';",
                -1, &statement, NULL) != SQLITE_OK) {
            goto rollback;
        }
        sqlite3_bind_int(statement, 1, event->persons);
        if (sqlite3_step(statement) != SQLITE_DONE) {
            goto rollback;
        }
        sqlite3_finalize(statement);
        statement = NULL;
        if (prune_blackbox(db, capacity) != 0) {
            goto rollback;
        }
    }
    if (sqlite_exec_checked(db, "COMMIT;") != 0) {
        goto rollback_no_transaction;
    }
    if (inserted == 0) {
        unlink(persistent_snapshot);
    }
    *stored_guard_armed = guard_armed;
    fprintf(stdout, "Black box recorded event_id=%llu persons=%d guard=%s capacity=%d\n",
            event->event_id, event->persons, guard_armed ? "armed" : "disarmed", capacity);
    fflush(stdout);
    return 0;

rollback:
    if (statement != NULL) {
        sqlite3_finalize(statement);
    }
    (void)sqlite_exec_checked(db, "ROLLBACK;");
rollback_no_transaction:
    unlink(persistent_snapshot);
    result = -1;
    return result;
}


static int write_vision_control(const char *control_file,
                                bool thermal_active,
                                int normal_max_fps,
                                int normal_detection_width,
                                int normal_output_width,
                                int thermal_max_fps,
                                int thermal_detection_width,
                                int thermal_output_width,
                                double temperature_c,
                                double threshold_c)
{
    cJSON *root = cJSON_CreateObject();
    char timestamp[64];
    char *text;
    int result;
    if (root == NULL) {
        return -1;
    }
    create_timestamp(timestamp, sizeof(timestamp));
    cJSON_AddStringToObject(root, "mode", thermal_active ? "thermal" : "normal");
    cJSON_AddNumberToObject(root, "max_fps", thermal_active ? thermal_max_fps : normal_max_fps);
    cJSON_AddNumberToObject(root, "detection_width",
                            thermal_active ? thermal_detection_width : normal_detection_width);
    cJSON_AddNumberToObject(root, "output_width",
                            thermal_active ? thermal_output_width : normal_output_width);
    cJSON_AddNumberToObject(root, "temperature_c", temperature_c);
    cJSON_AddNumberToObject(root, "threshold_c", threshold_c);
    cJSON_AddStringToObject(root, "updated_at", timestamp);
    text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return -1;
    }
    result = atomic_write_text(control_file, text, 0644);
    free(text);
    return result;
}


static int write_thermal_status(const char *status_file,
                                bool active,
                                double temperature_c,
                                double threshold_c,
                                double hysteresis_c,
                                int target_fps,
                                int detection_width,
                                int output_width)
{
    cJSON *root = cJSON_CreateObject();
    char timestamp[64];
    char *text;
    int result;
    if (root == NULL) {
        return -1;
    }
    create_timestamp(timestamp, sizeof(timestamp));
    cJSON_AddBoolToObject(root, "thermal_active", active);
    cJSON_AddStringToObject(root, "mode", active ? "thermal" : "normal");
    cJSON_AddNumberToObject(root, "temperature_c", temperature_c);
    cJSON_AddNumberToObject(root, "high_threshold_c", threshold_c);
    cJSON_AddNumberToObject(root, "hysteresis_c", hysteresis_c);
    cJSON_AddNumberToObject(root, "target_max_fps", target_fps);
    cJSON_AddNumberToObject(root, "detection_width", detection_width);
    cJSON_AddNumberToObject(root, "output_width", output_width);
    cJSON_AddStringToObject(root, "updated_at", timestamp);
    text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return -1;
    }
    result = atomic_write_text(status_file, text, 0644);
    free(text);
    return result;
}


static void on_connect(struct mosquitto *mosquitto, void *user_data, int result_code)
{
    (void)user_data;
    if (result_code == 0) {
        char *online = build_status_payload("online");
        g_connected = 1;
        fprintf(stdout, "Connected to MQTT broker\n");
        if (online != NULL) {
            (void)publish_qos1(mosquitto, g_status_topic, online, true);
            free(online);
        }
    } else {
        g_connected = 0;
        fprintf(stderr, "MQTT connection rejected: %s\n", mosquitto_connack_string(result_code));
    }
}


static void on_disconnect(struct mosquitto *mosquitto, void *user_data, int result_code)
{
    (void)mosquitto;
    (void)user_data;
    g_connected = 0;
    if (!g_stop) {
        fprintf(stderr, "MQTT disconnected: %s\n", mosquitto_strerror(result_code));
    }
}


static void on_publish(struct mosquitto *mosquitto, void *user_data, int message_id)
{
    (void)mosquitto;
    (void)user_data;
    fprintf(stdout, "MQTT QoS acknowledgement mid=%d\n", message_id);
    fflush(stdout);
}


int main(void)
{
    const char *broker_host = get_env_or_default("SMART_GUARD_MQTT_HOST", DEFAULT_BROKER_HOST);
    const char *mqtt_username = getenv("SMART_GUARD_MQTT_USERNAME");
    const char *mqtt_password = getenv("SMART_GUARD_MQTT_PASSWORD");
    const char *telemetry_url = get_env_or_default("SMART_GUARD_TELEMETRY_URL", DEFAULT_TELEMETRY_URL);
    const char *event_file = get_env_or_default("SMART_GUARD_EVENT_FILE", DEFAULT_EVENT_FILE);
    const char *system_event_file = get_env_or_default("SMART_GUARD_SYSTEM_EVENT_FILE", DEFAULT_SYSTEM_EVENT_FILE);
    const char *guard_state_file = get_env_or_default("SMART_GUARD_GUARD_STATE_FILE", DEFAULT_GUARD_STATE_FILE);
    const char *database_file = get_env_or_default("SMART_GUARD_BLACKBOX_DB", DEFAULT_DATABASE_FILE);
    const char *blackbox_dir = get_env_or_default("SMART_GUARD_BLACKBOX_DIR", DEFAULT_BLACKBOX_DIR);
    const char *control_file = get_env_or_default("SMART_GUARD_VISION_CONTROL_FILE", DEFAULT_CONTROL_FILE);
    const char *thermal_status_file = get_env_or_default("SMART_GUARD_THERMAL_STATUS_FILE", DEFAULT_THERMAL_STATUS_FILE);
    const char *thermal_state_file = get_env_or_default("SMART_GUARD_THERMAL_STATE_FILE", DEFAULT_THERMAL_STATE_FILE);
    const char *email_state_file = get_env_or_default("SMART_GUARD_EMAIL_STATE_FILE", DEFAULT_EMAIL_STATE_FILE);
    const char *event_state_file = get_env_or_default("SMART_GUARD_EVENT_STATE_FILE", DEFAULT_EVENT_STATE_FILE);
    const char *email_event_state_file = get_env_or_default(
        "SMART_GUARD_EMAIL_EVENT_STATE_FILE", DEFAULT_EMAIL_EVENT_STATE_FILE);
    const char *seen_event_state_file = get_env_or_default(
        "SMART_GUARD_SEEN_EVENT_STATE_FILE", DEFAULT_SEEN_EVENT_STATE_FILE);
    const char *alarm_mqtt_state_file = get_env_or_default(
        "SMART_GUARD_ALARM_MQTT_STATE_FILE", DEFAULT_ALARM_MQTT_STATE_FILE);
    const char *alarm_email_state_file = get_env_or_default(
        "SMART_GUARD_ALARM_EMAIL_STATE_FILE", DEFAULT_ALARM_EMAIL_STATE_FILE);
    const char *system_event_state_file = get_env_or_default(
        "SMART_GUARD_SYSTEM_EVENT_STATE_FILE", DEFAULT_SYSTEM_EVENT_STATE_FILE);

    int broker_port = get_env_int("SMART_GUARD_MQTT_PORT", DEFAULT_BROKER_PORT, 1, 65535);
    int interval_seconds = get_env_int("SMART_GUARD_MQTT_INTERVAL", DEFAULT_INTERVAL_SECONDS, 1, 3600);
    int poll_milliseconds = get_env_int("SMART_GUARD_EVENT_POLL_MS", DEFAULT_EVENT_POLL_MILLISECONDS, 50, 5000);
    int debounce_seconds = get_env_int("SMART_GUARD_EMAIL_DEBOUNCE_SECONDS",
                                       DEFAULT_EMAIL_DEBOUNCE_SECONDS, 30, 86400);
    int blackbox_capacity = get_env_int("SMART_GUARD_BLACKBOX_CAPACITY",
                                        DEFAULT_BLACKBOX_CAPACITY, 5, 100000);
    double thermal_high_c = get_env_double("SMART_GUARD_THERMAL_HIGH_C",
                                           DEFAULT_THERMAL_HIGH_C, 20.0, 150.0);
    double thermal_hysteresis_c = get_env_double("SMART_GUARD_THERMAL_HYSTERESIS_C",
                                                 DEFAULT_THERMAL_HYSTERESIS_C, 1.0, 30.0);
    int thermal_max_fps = get_env_int("SMART_GUARD_THERMAL_MAX_FPS",
                                      DEFAULT_THERMAL_MAX_FPS, 1, 60);
    int thermal_detection_width = get_env_int("SMART_GUARD_THERMAL_DETECTION_WIDTH",
                                              DEFAULT_THERMAL_DETECTION_WIDTH, 160, 4096);
    int thermal_output_width = get_env_int("SMART_GUARD_THERMAL_OUTPUT_WIDTH",
                                           DEFAULT_THERMAL_OUTPUT_WIDTH, 0, 4096);
    int normal_max_fps = get_env_int("SMART_GUARD_NORMAL_MAX_FPS",
                                     DEFAULT_NORMAL_MAX_FPS, 0, 120);
    int normal_detection_width = get_env_int("SMART_GUARD_NORMAL_DETECTION_WIDTH",
                                             DEFAULT_NORMAL_DETECTION_WIDTH, 160, 4096);
    int normal_output_width = get_env_int("SMART_GUARD_NORMAL_OUTPUT_WIDTH",
                                          DEFAULT_NORMAL_OUTPUT_WIDTH, 0, 4096);
    bool email_enabled = get_env_bool("SMART_GUARD_EMAIL_ENABLED", true);
    bool mqtt_enabled = get_env_bool("SMART_GUARD_MQTT_ENABLED", true);

    char client_id[160];
    char telemetry_topic[256];
    char persons_topic[256];
    char alarm_topic[256];
    char *lwt_payload = NULL;
    struct mosquitto *mosquitto = NULL;
    sqlite3 *database = NULL;
    struct sigaction action;
    int mosquitto_result;
    int exit_code = EXIT_FAILURE;
    long long next_periodic_ms;
    long long next_alarm_email_retry_ms = 0LL;
    long long next_system_email_retry_ms = 0LL;
    unsigned long long last_mqtt_event;
    unsigned long long last_email_event;
    unsigned long long last_email_epoch;
    unsigned long long last_seen_event;
    unsigned long long last_alarm_mqtt_event;
    unsigned long long last_alarm_email_event;
    unsigned long long last_system_event;
    bool thermal_active;
    telemetry_summary_t latest_telemetry;

    memset(&latest_telemetry, 0, sizeof(latest_telemetry));
    snprintf(g_student_id, sizeof(g_student_id), "%s",
             get_env_or_default("SMART_GUARD_STUDENT_ID", DEFAULT_STUDENT_ID));
    snprintf(client_id, sizeof(client_id), "smart-guard-s4-%s-%ld", g_student_id, (long)getpid());
    snprintf(telemetry_topic, sizeof(telemetry_topic), "telemetry/%s/home", g_student_id);
    snprintf(persons_topic, sizeof(persons_topic), "persons/%s/home", g_student_id);
    snprintf(alarm_topic, sizeof(alarm_topic), "alarm/%s/home", g_student_id);
    snprintf(g_status_topic, sizeof(g_status_topic), "status/%s/home", g_student_id);

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    if (ensure_parent_directory(guard_state_file, 0750) != 0) {
        fprintf(stderr, "Cannot initialize state directory: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    if (access(guard_state_file, F_OK) != 0 && write_boolean_state(guard_state_file, false) != 0) {
        fprintf(stderr, "Cannot initialize guard state: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "curl_global_init failed\n");
        return EXIT_FAILURE;
    }
    database = open_blackbox_database(database_file);
    if (database == NULL) {
        goto cleanup;
    }

    if (mqtt_enabled) {
        if (mqtt_username == NULL || mqtt_password == NULL || mqtt_username[0] == '\0' ||
            mqtt_password[0] == '\0' || strstr(mqtt_password, "REPLACE_") != NULL) {
            fprintf(stderr, "MQTT credentials are incomplete; continuing with MQTT disabled\n");
            mqtt_enabled = false;
        }
    }
    if (mqtt_enabled) {
        mosquitto_result = mosquitto_lib_init();
        if (mosquitto_result != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "mosquitto_lib_init failed: %s\n", mosquitto_strerror(mosquitto_result));
            goto cleanup;
        }
        mosquitto = mosquitto_new(client_id, true, NULL);
        if (mosquitto == NULL) {
            fprintf(stderr, "mosquitto_new failed\n");
            goto cleanup_mosquitto;
        }
        mosquitto_result = mosquitto_username_pw_set(mosquitto, mqtt_username, mqtt_password);
        if (mosquitto_result != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "Cannot configure MQTT credentials: %s\n", mosquitto_strerror(mosquitto_result));
            goto cleanup_mosquitto;
        }
        lwt_payload = build_status_payload("offline-unexpected");
        if (lwt_payload == NULL) {
            fprintf(stderr, "Cannot create LWT payload\n");
            goto cleanup_mosquitto;
        }
        mosquitto_result = mosquitto_will_set(mosquitto, g_status_topic, (int)strlen(lwt_payload),
                                              lwt_payload, 1, true);
        if (mosquitto_result != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "Cannot configure LWT: %s\n", mosquitto_strerror(mosquitto_result));
            goto cleanup_mosquitto;
        }
        mosquitto_connect_callback_set(mosquitto, on_connect);
        mosquitto_disconnect_callback_set(mosquitto, on_disconnect);
        mosquitto_publish_callback_set(mosquitto, on_publish);
        mosquitto_reconnect_delay_set(mosquitto, 2U, 30U, true);
        mosquitto_result = mosquitto_connect_async(mosquitto, broker_host, broker_port, 30);
        if (mosquitto_result != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "Initial MQTT connection failed: %s\n", mosquitto_strerror(mosquitto_result));
            goto cleanup_mosquitto;
        }
        mosquitto_result = mosquitto_loop_start(mosquitto);
        if (mosquitto_result != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "Cannot start MQTT loop: %s\n", mosquitto_strerror(mosquitto_result));
            goto cleanup_mosquitto;
        }
    }

    last_mqtt_event = read_unsigned_state(event_state_file);
    last_email_event = read_unsigned_state(email_event_state_file);
    last_email_epoch = read_unsigned_state(email_state_file);
    last_seen_event = read_unsigned_state(seen_event_state_file);
    last_alarm_mqtt_event = read_unsigned_state(alarm_mqtt_state_file);
    last_alarm_email_event = read_unsigned_state(alarm_email_state_file);
    last_system_event = read_unsigned_state(system_event_state_file);
    thermal_active = read_boolean_state(thermal_state_file, false);
    (void)write_vision_control(control_file, thermal_active,
                               normal_max_fps, normal_detection_width, normal_output_width,
                               thermal_max_fps, thermal_detection_width, thermal_output_width,
                               0.0, thermal_high_c);
    (void)write_thermal_status(thermal_status_file, thermal_active, 0.0,
                               thermal_high_c, thermal_hysteresis_c,
                               thermal_active ? thermal_max_fps : normal_max_fps,
                               thermal_active ? thermal_detection_width : normal_detection_width,
                               thermal_active ? thermal_output_width : normal_output_width);
    next_periodic_ms = monotonic_milliseconds();

    fprintf(stdout,
            "Smart Guard Section 4 controller started\n"
            "Broker: %s:%d MQTT=%s\nTelemetry: %s\nEvent: %s\nSystem event: %s\n"
            "Topics: %s, %s, %s, %s\nEmail=%s debounce=%ds\n"
            "Guard state: %s\nBlack box: %s capacity=%d\n"
            "Thermal: high=%.1fC hysteresis=%.1fC profile=%dfps/%dpx output=%dpx\n",
            broker_host, broker_port, mqtt_enabled ? "enabled" : "disabled",
            telemetry_url, event_file, system_event_file,
            telemetry_topic, persons_topic, alarm_topic, g_status_topic,
            email_enabled ? "enabled" : "disabled", debounce_seconds,
            guard_state_file, database_file, blackbox_capacity,
            thermal_high_c, thermal_hysteresis_c,
            thermal_max_fps, thermal_detection_width, thermal_output_width);
    fflush(stdout);

    while (!g_stop) {
        long long now_ms = monotonic_milliseconds();

        if (now_ms >= next_periodic_ms) {
            char *telemetry_json = fetch_telemetry_json(telemetry_url);
            if (telemetry_json != NULL) {
                telemetry_summary_t telemetry;
                if (parse_telemetry_summary(telemetry_json, &telemetry)) {
                    latest_telemetry = telemetry;
                    if (mqtt_enabled && g_connected) {
                        char *persons_payload = build_persons_payload(&telemetry, NULL);
                        (void)publish_qos1(mosquitto, telemetry_topic, telemetry_json, false);
                        if (persons_payload != NULL) {
                            (void)publish_qos1(mosquitto, persons_topic, persons_payload, false);
                            free(persons_payload);
                        }
                    }

                    if (telemetry.temperature_available) {
                        bool next_thermal = thermal_active;
                        if (!thermal_active && telemetry.temperature_c >= thermal_high_c) {
                            next_thermal = true;
                        } else if (thermal_active &&
                                   telemetry.temperature_c <= thermal_high_c - thermal_hysteresis_c) {
                            next_thermal = false;
                        }
                        if (next_thermal != thermal_active) {
                            char subject[512];
                            char body[2048];
                            thermal_active = next_thermal;
                            if (write_boolean_state(thermal_state_file, thermal_active) != 0) {
                                fprintf(stderr, "Cannot persist thermal state: %s\n", strerror(errno));
                            }
                            if (write_vision_control(control_file, thermal_active,
                                                     normal_max_fps, normal_detection_width, normal_output_width,
                                                     thermal_max_fps, thermal_detection_width, thermal_output_width,
                                                     telemetry.temperature_c, thermal_high_c) != 0) {
                                fprintf(stderr, "Cannot write vision control: %s\n", strerror(errno));
                            }
                            snprintf(subject, sizeof(subject), "[THERMAL] Smart Guard %s %s",
                                     g_student_id, thermal_active ? "throttling enabled" : "normal mode restored");
                            snprintf(body, sizeof(body),
                                     "Adaptive thermal management transition\r\n\r\n"
                                     "Student ID: %s\r\nTimestamp: %s\r\nTemperature: %.3f C\r\n"
                                     "High threshold: %.3f C\r\nHysteresis: %.3f C\r\n"
                                     "New mode: %s\r\nTarget max FPS: %d\r\nDetection width: %d\r\nOutput width: %d\r\n",
                                     g_student_id, telemetry.timestamp, telemetry.temperature_c,
                                     thermal_high_c, thermal_hysteresis_c,
                                     thermal_active ? "thermal" : "normal",
                                     thermal_active ? thermal_max_fps : normal_max_fps,
                                     thermal_active ? thermal_detection_width : normal_detection_width,
                                     thermal_active ? thermal_output_width : normal_output_width);
                            if (email_enabled && send_email_message(subject, body, NULL, NULL) != 0) {
                                fprintf(stderr, "Thermal transition email failed\n");
                            }
                            fprintf(stdout, "Thermal transition active=%s temp=%.3fC\n",
                                    thermal_active ? "true" : "false", telemetry.temperature_c);
                            fflush(stdout);
                        }
                        (void)write_thermal_status(
                            thermal_status_file, thermal_active, telemetry.temperature_c,
                            thermal_high_c, thermal_hysteresis_c,
                            thermal_active ? thermal_max_fps : normal_max_fps,
                            thermal_active ? thermal_detection_width : normal_detection_width,
                            thermal_active ? thermal_output_width : normal_output_width);
                    }
                }
                free(telemetry_json);
            }
            next_periodic_ms = now_ms + (long long)interval_seconds * 1000LL;
        }

        {
            char *event_json = read_entire_file(event_file, MAX_JSON_FILE);
            detection_event_t event;
            if (event_json != NULL && parse_detection_event(event_json, &event)) {
                telemetry_summary_t telemetry = latest_telemetry;
                bool guard_for_event = false;
                if (!telemetry.valid) {
                    memset(&telemetry, 0, sizeof(telemetry));
                    telemetry.persons = event.persons;
                    snprintf(telemetry.timestamp, sizeof(telemetry.timestamp), "%s", event.timestamp);
                }

                if (event.event_id != last_seen_event) {
                    bool guard_now = read_boolean_state(guard_state_file, false);
                    if (record_blackbox_event(database, &event, &telemetry, guard_now,
                                              blackbox_capacity, blackbox_dir,
                                              &guard_for_event) == 0) {
                        last_seen_event = event.event_id;
                        if (write_unsigned_state(seen_event_state_file, last_seen_event) != 0) {
                            fprintf(stderr, "Cannot persist last seen event: %s\n", strerror(errno));
                        }
                    }
                }
                if (database_guard_for_event(database, event.event_id, &guard_for_event) != 0) {
                    guard_for_event = false;
                }

                if (mqtt_enabled && g_connected && event.event_id != last_mqtt_event) {
                    char *event_payload = build_persons_payload(&telemetry, &event);
                    if (event_payload != NULL) {
                        if (publish_qos1(mosquitto, persons_topic, event_payload, false) == 0) {
                            last_mqtt_event = event.event_id;
                            if (write_unsigned_state(event_state_file, last_mqtt_event) != 0) {
                                fprintf(stderr, "Cannot persist MQTT event state: %s\n", strerror(errno));
                            }
                        }
                        free(event_payload);
                    }
                }

                if (guard_for_event) {
                    if (mqtt_enabled && g_connected && event.event_id != last_alarm_mqtt_event) {
                        char *alarm_payload = build_alarm_payload(&event, &telemetry,
                                                                  "guard_intrusion", "human detected while armed");
                        if (alarm_payload != NULL) {
                            if (publish_qos1(mosquitto, alarm_topic, alarm_payload, false) == 0) {
                                last_alarm_mqtt_event = event.event_id;
                                if (write_unsigned_state(alarm_mqtt_state_file, last_alarm_mqtt_event) != 0) {
                                    fprintf(stderr, "Cannot persist alarm MQTT state: %s\n", strerror(errno));
                                }
                            }
                            free(alarm_payload);
                        }
                    }
                    if (email_enabled && event.event_id != last_alarm_email_event &&
                        now_ms >= next_alarm_email_retry_ms) {
                        if (send_detection_email(&event, &telemetry, true) == 0) {
                            last_alarm_email_event = event.event_id;
                            if (write_unsigned_state(alarm_email_state_file, last_alarm_email_event) != 0) {
                                fprintf(stderr, "Cannot persist alarm email state: %s\n", strerror(errno));
                            }
                        } else {
                            next_alarm_email_retry_ms = now_ms + 10000LL;
                            fprintf(stderr, "Alarm email will be retried in 10 seconds event_id=%llu\n",
                                    event.event_id);
                        }
                    }
                } else if (email_enabled && event.event_id != last_email_event) {
                    unsigned long long now_epoch = (unsigned long long)time(NULL);
                    if (last_email_epoch != 0ULL &&
                        now_epoch < last_email_epoch + (unsigned long long)debounce_seconds) {
                        fprintf(stdout,
                                "Email debounce active; event_id=%llu suppressed for %llu more second(s)\n",
                                event.event_id,
                                last_email_epoch + (unsigned long long)debounce_seconds - now_epoch);
                        last_email_event = event.event_id;
                        if (write_unsigned_state(email_event_state_file, last_email_event) != 0) {
                            fprintf(stderr, "Cannot persist suppressed email state: %s\n", strerror(errno));
                        }
                    } else if (send_detection_email(&event, &telemetry, false) == 0) {
                        last_email_epoch = now_epoch;
                        last_email_event = event.event_id;
                        if (write_unsigned_state(email_state_file, last_email_epoch) != 0 ||
                            write_unsigned_state(email_event_state_file, last_email_event) != 0) {
                            fprintf(stderr, "Cannot persist email debounce state: %s\n", strerror(errno));
                        }
                    }
                }
            }
            free(event_json);
        }

        {
            char *system_json = read_entire_file(system_event_file, MAX_JSON_FILE);
            system_event_t system_event;
            if (system_json != NULL && parse_system_event(system_json, &system_event) &&
                system_event.event_id != last_system_event && now_ms >= next_system_email_retry_ms) {
                char subject[512];
                char body[3072];
                snprintf(subject, sizeof(subject), "[CAMERA TAMPER] Smart Guard %s", g_student_id);
                snprintf(body, sizeof(body),
                         "Camera tamper watchdog alert\r\n\r\n"
                         "Student ID: %s\r\nEvent type: %s\r\nTimestamp: %s\r\n"
                         "Message: %s\r\nService: %s\r\nLast-frame age: %.3f seconds\r\n"
                         "Action: the watchdog requested a service restart.\r\n",
                         g_student_id, system_event.type, system_event.timestamp,
                         system_event.message, system_event.service,
                         system_event.frame_age_seconds);
                if (mqtt_enabled && g_connected) {
                    char *payload = build_alarm_payload(NULL, &latest_telemetry,
                                                        "camera_tamper", system_event.message);
                    if (payload != NULL) {
                        (void)publish_qos1(mosquitto, alarm_topic, payload, false);
                        free(payload);
                    }
                }
                if (!email_enabled || send_email_message(subject, body, NULL, NULL) == 0) {
                    last_system_event = system_event.event_id;
                    if (write_unsigned_state(system_event_state_file, last_system_event) != 0) {
                        fprintf(stderr, "Cannot persist system event state: %s\n", strerror(errno));
                    }
                } else {
                    next_system_email_retry_ms = now_ms + 10000LL;
                    fprintf(stderr, "Camera-tamper email will be retried in 10 seconds\n");
                }
            }
            free(system_json);
        }

        sleep_milliseconds(poll_milliseconds);
    }

    if (mqtt_enabled && mosquitto != NULL) {
        if (g_connected) {
            char *clean = build_status_payload("offline-clean");
            if (clean != NULL) {
                (void)publish_qos1(mosquitto, g_status_topic, clean, true);
                free(clean);
            }
            sleep_milliseconds(300);
            (void)mosquitto_disconnect(mosquitto);
        }
        (void)mosquitto_loop_stop(mosquitto, false);
    }
    exit_code = EXIT_SUCCESS;

cleanup_mosquitto:
    free(lwt_payload);
    if (mosquitto != NULL) {
        mosquitto_destroy(mosquitto);
    }
    if (mqtt_enabled) {
        mosquitto_lib_cleanup();
    }
cleanup:
    if (database != NULL) {
        sqlite3_close(database);
    }
    curl_global_cleanup();
    return exit_code;
}
