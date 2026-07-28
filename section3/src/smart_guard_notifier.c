#define _POSIX_C_SOURCE 200809L

#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <mosquitto.h>

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_BROKER_HOST "192.168.122.1"
#define DEFAULT_BROKER_PORT 1883
#define DEFAULT_STUDENT_ID "401102553"
#define DEFAULT_INTERVAL_SECONDS 2
#define DEFAULT_TELEMETRY_URL "http://127.0.0.1:18080/api/v1/telemetry"
#define DEFAULT_EVENT_FILE "/run/smart-guard/detection_event.json"
#define DEFAULT_EMAIL_STATE_FILE "/var/lib/smart-guard/last_email_epoch"
#define DEFAULT_EVENT_STATE_FILE "/var/lib/smart-guard/last_event_id"
#define DEFAULT_EMAIL_EVENT_STATE_FILE "/var/lib/smart-guard/last_email_event_id"
#define DEFAULT_EMAIL_DEBOUNCE_SECONDS 30
#define DEFAULT_EVENT_POLL_MILLISECONDS 200
#define MAX_HTTP_RESPONSE (2U * 1024U * 1024U)


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
        return 0;
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
    char temporary[PATH_MAX];
    char line[64];
    FILE *file;

    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    snprintf(line, sizeof(line), "%llu\n", value);

    file = fopen(temporary, "wb");
    if (file == NULL) {
        return -1;
    }
    if (fwrite(line, 1U, strlen(line), file) != strlen(line) || fflush(file) != 0) {
        int saved_errno = errno;
        fclose(file);
        unlink(temporary);
        errno = saved_errno;
        return -1;
    }
    if (fsync(fileno(file)) != 0) {
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


static size_t smart_guard_curl_write_callback(void *contents, size_t size, size_t count, void *user_data)
{
    size_t received = size * count;
    memory_buffer_t *buffer = user_data;
    char *new_data;

    if (received == 0U) {
        return 0U;
    }
    if (buffer->size > MAX_HTTP_RESPONSE - received - 1U) {
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
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, smart_guard_curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 4L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "smart-guard-section3-notifier/1.0");

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


static int publish_qos1(struct mosquitto *mosquitto, const char *topic,
                        const char *payload, bool retain)
{
    int message_id = 0;
    int result = mosquitto_publish(
        mosquitto,
        &message_id,
        topic,
        (int)strlen(payload),
        payload,
        1,
        retain);
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


static int send_detection_email(const detection_event_t *event,
                                const telemetry_summary_t *telemetry)
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
    char subject_header[384];
    char mail_from[384];
    char body[2048];
    struct stat snapshot_stat;
    int return_code = -1;

    if (smtp_url == NULL || smtp_username == NULL || smtp_password == NULL ||
        email_from == NULL || email_to == NULL || smtp_url[0] == '\0' ||
        smtp_username[0] == '\0' || smtp_password[0] == '\0' ||
        email_from[0] == '\0' || email_to[0] == '\0') {
        fprintf(stderr, "Email is enabled but SMTP/email variables are incomplete\n");
        return -1;
    }
    if (stat(event->snapshot_path, &snapshot_stat) != 0 || !S_ISREG(snapshot_stat.st_mode)) {
        fprintf(stderr, "Detection snapshot is missing: %s\n", event->snapshot_path);
        return -1;
    }

    snprintf(mail_from, sizeof(mail_from), "<%s>", email_from);
    snprintf(from_header, sizeof(from_header), "From: Smart Guard <%s>", email_from);
    snprintf(to_header, sizeof(to_header), "To: <%s>", email_to);
    snprintf(subject_header, sizeof(subject_header),
             "Subject: Smart Guard detection %s - %d person(s)", g_student_id, event->persons);
    if (telemetry->temperature_available) {
        snprintf(body, sizeof(body),
                 "Smart Guard person detection\r\n\r\n"
                 "Student ID: %s\r\nPersons: %d\r\nTimestamp: %s\r\n"
                 "CPU temperature: %.3f C\r\nVision FPS: %.3f\r\nEvent ID: %llu\r\n",
                 g_student_id, event->persons, event->timestamp,
                 telemetry->temperature_c, event->vision_fps, event->event_id);
    } else {
        snprintf(body, sizeof(body),
                 "Smart Guard person detection\r\n\r\n"
                 "Student ID: %s\r\nPersons: %d\r\nTimestamp: %s\r\n"
                 "CPU temperature: unavailable\r\nVision FPS: %.3f\r\nEvent ID: %llu\r\n",
                 g_student_id, event->persons, event->timestamp,
                 event->vision_fps, event->event_id);
    }

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
    part = curl_mime_addpart(mime);
    if (part == NULL || curl_mime_filedata(part, event->snapshot_path) != CURLE_OK ||
        curl_mime_filename(part, path_basename(event->snapshot_path)) != CURLE_OK ||
        curl_mime_type(part, "image/jpeg") != CURLE_OK ||
        curl_mime_encoder(part, "base64") != CURLE_OK) {
        goto cleanup;
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
    fprintf(stdout, "Detection email sent event_id=%llu to=%s\n", event->event_id, email_to);
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
    const char *email_state_file = get_env_or_default("SMART_GUARD_EMAIL_STATE_FILE", DEFAULT_EMAIL_STATE_FILE);
    const char *event_state_file = get_env_or_default("SMART_GUARD_EVENT_STATE_FILE", DEFAULT_EVENT_STATE_FILE);
    const char *email_event_state_file = get_env_or_default(
        "SMART_GUARD_EMAIL_EVENT_STATE_FILE", DEFAULT_EMAIL_EVENT_STATE_FILE);
    int broker_port = get_env_int("SMART_GUARD_MQTT_PORT", DEFAULT_BROKER_PORT, 1, 65535);
    int interval_seconds = get_env_int("SMART_GUARD_MQTT_INTERVAL", DEFAULT_INTERVAL_SECONDS, 1, 3600);
    int poll_milliseconds = get_env_int("SMART_GUARD_EVENT_POLL_MS", DEFAULT_EVENT_POLL_MILLISECONDS, 50, 5000);
    int debounce_seconds = get_env_int("SMART_GUARD_EMAIL_DEBOUNCE_SECONDS",
                                       DEFAULT_EMAIL_DEBOUNCE_SECONDS, 30, 86400);
    bool email_enabled = get_env_bool("SMART_GUARD_EMAIL_ENABLED", true);
    char client_id[160];
    char telemetry_topic[256];
    char persons_topic[256];
    char *lwt_payload = NULL;
    struct mosquitto *mosquitto = NULL;
    struct sigaction action;
    int result;
    int exit_code = EXIT_FAILURE;
    long long next_periodic_ms;
    unsigned long long last_processed_event;
    unsigned long long last_email_event;
    unsigned long long last_email_epoch;
    long long next_email_retry_ms = 0LL;

    snprintf(g_student_id, sizeof(g_student_id), "%s",
             get_env_or_default("SMART_GUARD_STUDENT_ID", DEFAULT_STUDENT_ID));

    if (mqtt_username == NULL || mqtt_password == NULL || mqtt_username[0] == '\0' ||
        mqtt_password[0] == '\0' || strstr(mqtt_password, "REPLACE_") != NULL) {
        fprintf(stderr, "MQTT username/password are not configured\n");
        return EXIT_FAILURE;
    }

    snprintf(client_id, sizeof(client_id), "smart-guard-s3-%s-%ld", g_student_id, (long)getpid());
    snprintf(telemetry_topic, sizeof(telemetry_topic), "telemetry/%s/home", g_student_id);
    snprintf(persons_topic, sizeof(persons_topic), "persons/%s/home", g_student_id);
    snprintf(g_status_topic, sizeof(g_status_topic), "status/%s/home", g_student_id);

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "curl_global_init failed\n");
        return EXIT_FAILURE;
    }
    result = mosquitto_lib_init();
    if (result != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "mosquitto_lib_init failed: %s\n", mosquitto_strerror(result));
        curl_global_cleanup();
        return EXIT_FAILURE;
    }

    mosquitto = mosquitto_new(client_id, true, NULL);
    if (mosquitto == NULL) {
        fprintf(stderr, "mosquitto_new failed\n");
        goto cleanup;
    }
    result = mosquitto_username_pw_set(mosquitto, mqtt_username, mqtt_password);
    if (result != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Cannot configure MQTT credentials: %s\n", mosquitto_strerror(result));
        goto cleanup;
    }

    lwt_payload = build_status_payload("offline-unexpected");
    if (lwt_payload == NULL) {
        fprintf(stderr, "Cannot create LWT payload\n");
        goto cleanup;
    }
    result = mosquitto_will_set(mosquitto, g_status_topic, (int)strlen(lwt_payload),
                                lwt_payload, 1, true);
    if (result != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Cannot configure LWT: %s\n", mosquitto_strerror(result));
        goto cleanup;
    }

    mosquitto_connect_callback_set(mosquitto, on_connect);
    mosquitto_disconnect_callback_set(mosquitto, on_disconnect);
    mosquitto_publish_callback_set(mosquitto, on_publish);
    mosquitto_reconnect_delay_set(mosquitto, 2U, 30U, true);

    result = mosquitto_connect_async(mosquitto, broker_host, broker_port, 30);
    if (result != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Initial MQTT connection failed: %s\n", mosquitto_strerror(result));
        goto cleanup;
    }
    result = mosquitto_loop_start(mosquitto);
    if (result != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Cannot start MQTT loop: %s\n", mosquitto_strerror(result));
        goto cleanup;
    }

    last_processed_event = read_unsigned_state(event_state_file);
    last_email_event = read_unsigned_state(email_event_state_file);
    last_email_epoch = read_unsigned_state(email_state_file);
    next_periodic_ms = monotonic_milliseconds();

    fprintf(stdout,
            "Smart Guard Section 3 notifier started\n"
            "Broker: %s:%d\nTelemetry: %s\nEvent file: %s\n"
            "Topics: %s, %s, %s\nEmail enabled: %s debounce=%ds\n",
            broker_host, broker_port, telemetry_url, event_file,
            telemetry_topic, persons_topic, g_status_topic,
            email_enabled ? "yes" : "no", debounce_seconds);
    fflush(stdout);

    while (!g_stop) {
        long long now_ms = monotonic_milliseconds();

        if (now_ms >= next_periodic_ms) {
            char *telemetry_json = fetch_telemetry_json(telemetry_url);
            if (telemetry_json != NULL) {
                telemetry_summary_t telemetry;
                if (parse_telemetry_summary(telemetry_json, &telemetry) && g_connected) {
                    char *persons_payload = build_persons_payload(&telemetry, NULL);
                    (void)publish_qos1(mosquitto, telemetry_topic, telemetry_json, false);
                    if (persons_payload != NULL) {
                        (void)publish_qos1(mosquitto, persons_topic, persons_payload, false);
                        free(persons_payload);
                    }
                }
                free(telemetry_json);
            }
            next_periodic_ms = now_ms + (long long)interval_seconds * 1000LL;
        }

        {
            char *event_json = read_entire_file(event_file, 64U * 1024U);
            detection_event_t event;
            if (event_json != NULL && parse_detection_event(event_json, &event) &&
                (event.event_id != last_processed_event ||
                 (email_enabled && event.event_id != last_email_event))) {
                char *telemetry_json = fetch_telemetry_json(telemetry_url);
                telemetry_summary_t telemetry;
                bool telemetry_ok = telemetry_json != NULL &&
                                    parse_telemetry_summary(telemetry_json, &telemetry);

                if (!telemetry_ok) {
                    memset(&telemetry, 0, sizeof(telemetry));
                    snprintf(telemetry.timestamp, sizeof(telemetry.timestamp), "%s", event.timestamp);
                    telemetry.persons = event.persons;
                }

                if (event.event_id != last_processed_event && g_connected) {
                    char *event_payload = build_persons_payload(&telemetry, &event);
                    if (event_payload != NULL) {
                        if (publish_qos1(mosquitto, persons_topic, event_payload, false) == 0) {
                            last_processed_event = event.event_id;
                            if (write_unsigned_state(event_state_file, last_processed_event) != 0) {
                                fprintf(stderr, "Cannot persist MQTT event state: %s\n", strerror(errno));
                            }
                        }
                        free(event_payload);
                    }
                }

                if (email_enabled && event.event_id != last_email_event) {
                    unsigned long long now_epoch = (unsigned long long)time(NULL);
                    if (last_email_epoch != 0ULL &&
                        now_epoch < last_email_epoch + (unsigned long long)debounce_seconds) {
                        fprintf(stdout,
                                "Email debounce active; event_id=%llu suppressed for %llu more second(s)\n",
                                event.event_id,
                                last_email_epoch + (unsigned long long)debounce_seconds - now_epoch);
                        last_email_event = event.event_id;
                        if (write_unsigned_state(email_event_state_file, last_email_event) != 0) {
                            fprintf(stderr, "Cannot persist suppressed email event state: %s\n", strerror(errno));
                        }
                    } else if (now_ms >= next_email_retry_ms) {
                        if (send_detection_email(&event, &telemetry) == 0) {
                            last_email_epoch = now_epoch;
                            last_email_event = event.event_id;
                            if (write_unsigned_state(email_state_file, last_email_epoch) != 0) {
                                fprintf(stderr, "Cannot persist email debounce state: %s\n", strerror(errno));
                            }
                            if (write_unsigned_state(email_event_state_file, last_email_event) != 0) {
                                fprintf(stderr, "Cannot persist email event state: %s\n", strerror(errno));
                            }
                        } else {
                            next_email_retry_ms = now_ms + 10000LL;
                            fprintf(stderr, "Email will be retried in 10 seconds for event_id=%llu\n",
                                    event.event_id);
                        }
                    }
                }
                free(telemetry_json);
            }
            free(event_json);
        }

        sleep_milliseconds(poll_milliseconds);
    }

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
    exit_code = EXIT_SUCCESS;

cleanup:
    free(lwt_payload);
    if (mosquitto != NULL) {
        mosquitto_destroy(mosquitto);
    }
    mosquitto_lib_cleanup();
    curl_global_cleanup();
    return exit_code;
}
