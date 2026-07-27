#define _POSIX_C_SOURCE 200809L

#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <mosquitto.h>

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_BROKER_HOST       "192.168.122.1"
#define DEFAULT_BROKER_PORT       1883
#define DEFAULT_STUDENT_ID        "401102553"
#define DEFAULT_INTERVAL_SECONDS  2
#define DEFAULT_TELEMETRY_URL     "https://localhost/api/v1/telemetry"
#define DEFAULT_CA_FILE           "/etc/smart-guard/tls/server.crt"

typedef struct {
    char *data;
    size_t size;
} memory_buffer_t;

static volatile sig_atomic_t g_stop = 0;
static volatile sig_atomic_t g_connected = 0;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    g_stop = 1;
}

static const char *get_env_or_default(
    const char *name,
    const char *default_value)
{
    const char *value = getenv(name);

    if (value == NULL || value[0] == '\0') {
        return default_value;
    }

    return value;
}

static int get_env_int(
    const char *name,
    int default_value,
    int minimum,
    int maximum)
{
    const char *text = getenv(name);
    char *end = NULL;
    long value;

    if (text == NULL || text[0] == '\0') {
        return default_value;
    }

    errno = 0;
    value = strtol(text, &end, 10);

    if (errno != 0 ||
        end == text ||
        *end != '\0' ||
        value < minimum ||
        value > maximum) {
        fprintf(
            stderr,
            "Invalid value for %s; using %d\n",
            name,
            default_value);
        return default_value;
    }

    return (int)value;
}

static void create_timestamp(
    char *output,
    size_t output_size)
{
    time_t current_time;
    struct tm local_time;

    current_time = time(NULL);

    if (localtime_r(&current_time, &local_time) == NULL) {
        snprintf(output, output_size, "unknown");
        return;
    }

    if (strftime(
            output,
            output_size,
            "%Y-%m-%dT%H:%M:%S%z",
            &local_time) == 0) {
        snprintf(output, output_size, "unknown");
    }
}

static size_t smart_guard_curl_write_callback(
    void *contents,
    size_t size,
    size_t count,
    void *user_data)
{
    size_t received_size = size * count;
    memory_buffer_t *buffer = user_data;
    char *new_data;

    if (received_size == 0) {
        return 0;
    }

    if (buffer->size > SIZE_MAX - received_size - 1) {
        return 0;
    }

    new_data = realloc(
        buffer->data,
        buffer->size + received_size + 1);

    if (new_data == NULL) {
        return 0;
    }

    buffer->data = new_data;

    memcpy(
        buffer->data + buffer->size,
        contents,
        received_size);

    buffer->size += received_size;
    buffer->data[buffer->size] = '\0';

    return received_size;
}

static char *fetch_telemetry(
    const char *telemetry_url,
    const char *ca_file)
{
    CURL *curl;
    CURLcode result;
    long response_code = 0;
    memory_buffer_t response = {
        .data = NULL,
        .size = 0
    };

    response.data = calloc(1, 1);

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
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, ca_file);
    curl_easy_setopt(
        curl,
        CURLOPT_USERAGENT,
        "smart-guard-mqtt/1.0");

    result = curl_easy_perform(curl);

    if (result == CURLE_OK) {
        curl_easy_getinfo(
            curl,
            CURLINFO_RESPONSE_CODE,
            &response_code);
    }

    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        fprintf(
            stderr,
            "Telemetry request failed: %s\n",
            curl_easy_strerror(result));
        free(response.data);
        return NULL;
    }

    if (response_code != 200) {
        fprintf(
            stderr,
            "Telemetry endpoint returned HTTP %ld\n",
            response_code);
        free(response.data);
        return NULL;
    }

    return response.data;
}

static char *build_persons_payload(
    const char *telemetry_json,
    const char *student_id)
{
    cJSON *source = NULL;
    cJSON *payload = NULL;
    cJSON *persons_item;
    cJSON *temperature_item;
    cJSON *temperature_available_item;
    cJSON *timestamp_item;
    char *result = NULL;

    source = cJSON_Parse(telemetry_json);

    if (source == NULL) {
        fprintf(stderr, "Cannot parse telemetry JSON\n");
        return NULL;
    }

    persons_item = cJSON_GetObjectItemCaseSensitive(
        source,
        "persons");

    temperature_item = cJSON_GetObjectItemCaseSensitive(
        source,
        "cpu_temperature_c");

    temperature_available_item =
        cJSON_GetObjectItemCaseSensitive(
            source,
            "cpu_temperature_available");

    timestamp_item = cJSON_GetObjectItemCaseSensitive(
        source,
        "timestamp");

    if (!cJSON_IsNumber(persons_item) ||
        !cJSON_IsString(timestamp_item)) {
        fprintf(
            stderr,
            "Telemetry JSON is missing required fields\n");
        cJSON_Delete(source);
        return NULL;
    }

    payload = cJSON_CreateObject();

    if (payload == NULL) {
        cJSON_Delete(source);
        return NULL;
    }

    cJSON_AddStringToObject(
        payload,
        "student_id",
        student_id);

    cJSON_AddNumberToObject(
        payload,
        "persons",
        persons_item->valueint);

    if (cJSON_IsBool(temperature_available_item)) {
        cJSON_AddBoolToObject(
            payload,
            "temperature_available",
            cJSON_IsTrue(temperature_available_item));
    } else {
        cJSON_AddBoolToObject(
            payload,
            "temperature_available",
            false);
    }

    if (cJSON_IsNumber(temperature_item)) {
        cJSON_AddNumberToObject(
            payload,
            "temperature_c",
            temperature_item->valuedouble);
    } else {
        cJSON_AddNullToObject(
            payload,
            "temperature_c");
    }

    cJSON_AddStringToObject(
        payload,
        "timestamp",
        timestamp_item->valuestring);

    result = cJSON_PrintUnformatted(payload);

    cJSON_Delete(payload);
    cJSON_Delete(source);

    return result;
}

static char *build_status_payload(
    const char *student_id,
    const char *status)
{
    cJSON *payload;
    char timestamp[64];
    char *result;

    create_timestamp(timestamp, sizeof(timestamp));

    payload = cJSON_CreateObject();

    if (payload == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(
        payload,
        "student_id",
        student_id);

    cJSON_AddStringToObject(
        payload,
        "status",
        status);

    cJSON_AddStringToObject(
        payload,
        "timestamp",
        timestamp);

    result = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    return result;
}

static void on_connect(
    struct mosquitto *mosquitto,
    void *user_data,
    int result_code)
{
    const char *status_topic = user_data;

    if (result_code == 0) {
        const char *student_id =
            get_env_or_default(
                "SMART_GUARD_STUDENT_ID",
                DEFAULT_STUDENT_ID);

        char *online_payload =
            build_status_payload(student_id, "online");

        g_connected = 1;

        fprintf(
            stdout,
            "Connected to MQTT broker\n");

        if (online_payload != NULL) {
            int publish_result = mosquitto_publish(
                mosquitto,
                NULL,
                status_topic,
                (int)strlen(online_payload),
                online_payload,
                1,
                true);

            if (publish_result != MOSQ_ERR_SUCCESS) {
                fprintf(
                    stderr,
                    "Cannot publish online status: %s\n",
                    mosquitto_strerror(publish_result));
            }

            free(online_payload);
        }
    } else {
        g_connected = 0;

        fprintf(
            stderr,
            "MQTT connection rejected: %s\n",
            mosquitto_connack_string(result_code));
    }
}

static void on_disconnect(
    struct mosquitto *mosquitto,
    void *user_data,
    int result_code)
{
    (void)mosquitto;
    (void)user_data;

    g_connected = 0;

    if (!g_stop) {
        fprintf(
            stderr,
            "MQTT disconnected unexpectedly: %s\n",
            mosquitto_strerror(result_code));
    }
}

static int publish_qos1(
    struct mosquitto *mosquitto,
    const char *topic,
    const char *payload)
{
    int message_id = 0;
    int result;

    result = mosquitto_publish(
        mosquitto,
        &message_id,
        topic,
        (int)strlen(payload),
        payload,
        1,
        false);

    if (result != MOSQ_ERR_SUCCESS) {
        fprintf(
            stderr,
            "Publish failed for %s: %s\n",
            topic,
            mosquitto_strerror(result));
        return -1;
    }

    fprintf(
        stdout,
        "Published QoS1: topic=%s mid=%d\n",
        topic,
        message_id);

    return 0;
}

int main(void)
{
    const char *broker_host;
    const char *mqtt_username;
    const char *mqtt_password;
    const char *student_id;
    const char *telemetry_url;
    const char *ca_file;

    int broker_port;
    int interval_seconds;
    int result;
    int exit_code = EXIT_FAILURE;

    char client_id[128];
    char telemetry_topic[256];
    char persons_topic[256];
    char status_topic[256];

    char *offline_payload = NULL;

    struct mosquitto *mosquitto = NULL;
    struct sigaction signal_action;

    broker_host = get_env_or_default(
        "SMART_GUARD_MQTT_HOST",
        DEFAULT_BROKER_HOST);

    broker_port = get_env_int(
        "SMART_GUARD_MQTT_PORT",
        DEFAULT_BROKER_PORT,
        1,
        65535);

    mqtt_username = getenv("SMART_GUARD_MQTT_USERNAME");
    mqtt_password = getenv("SMART_GUARD_MQTT_PASSWORD");

    student_id = get_env_or_default(
        "SMART_GUARD_STUDENT_ID",
        DEFAULT_STUDENT_ID);

    telemetry_url = get_env_or_default(
        "SMART_GUARD_TELEMETRY_URL",
        DEFAULT_TELEMETRY_URL);

    ca_file = get_env_or_default(
        "SMART_GUARD_CA_FILE",
        DEFAULT_CA_FILE);

    interval_seconds = get_env_int(
        "SMART_GUARD_MQTT_INTERVAL",
        DEFAULT_INTERVAL_SECONDS,
        1,
        3600);

    if (mqtt_username == NULL ||
        mqtt_username[0] == '\0' ||
        mqtt_password == NULL ||
        mqtt_password[0] == '\0') {
        fprintf(
            stderr,
            "MQTT username/password are not configured\n");
        return EXIT_FAILURE;
    }

    snprintf(
        client_id,
        sizeof(client_id),
        "smart-guard-%s-%ld",
        student_id,
        (long)getpid());

    snprintf(
        telemetry_topic,
        sizeof(telemetry_topic),
        "telemetry/%s/home",
        student_id);

    snprintf(
        persons_topic,
        sizeof(persons_topic),
        "persons/%s/home",
        student_id);

    snprintf(
        status_topic,
        sizeof(status_topic),
        "status/%s/home",
        student_id);

    memset(&signal_action, 0, sizeof(signal_action));
    signal_action.sa_handler = handle_signal;

    sigemptyset(&signal_action.sa_mask);
    sigaction(SIGINT, &signal_action, NULL);
    sigaction(SIGTERM, &signal_action, NULL);

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "curl_global_init failed\n");
        return EXIT_FAILURE;
    }

    result = mosquitto_lib_init();

    if (result != MOSQ_ERR_SUCCESS) {
        fprintf(
            stderr,
            "mosquitto_lib_init failed: %s\n",
            mosquitto_strerror(result));
        curl_global_cleanup();
        return EXIT_FAILURE;
    }

    mosquitto = mosquitto_new(
        client_id,
        true,
        (void *)status_topic);

    if (mosquitto == NULL) {
        fprintf(stderr, "mosquitto_new failed\n");
        goto cleanup;
    }

    result = mosquitto_username_pw_set(
        mosquitto,
        mqtt_username,
        mqtt_password);

    if (result != MOSQ_ERR_SUCCESS) {
        fprintf(
            stderr,
            "Cannot configure MQTT credentials: %s\n",
            mosquitto_strerror(result));
        goto cleanup;
    }

    offline_payload =
        build_status_payload(student_id, "offline");

    if (offline_payload == NULL) {
        fprintf(stderr, "Cannot create LWT payload\n");
        goto cleanup;
    }

    result = mosquitto_will_set(
        mosquitto,
        status_topic,
        (int)strlen(offline_payload),
        offline_payload,
        1,
        true);

    if (result != MOSQ_ERR_SUCCESS) {
        fprintf(
            stderr,
            "Cannot configure MQTT LWT: %s\n",
            mosquitto_strerror(result));
        goto cleanup;
    }

    mosquitto_connect_callback_set(
        mosquitto,
        on_connect);

    mosquitto_disconnect_callback_set(
        mosquitto,
        on_disconnect);

    mosquitto_reconnect_delay_set(
        mosquitto,
        2,
        30,
        true);

    result = mosquitto_connect_async(
        mosquitto,
        broker_host,
        broker_port,
        30);

    if (result != MOSQ_ERR_SUCCESS) {
        fprintf(
            stderr,
            "Initial MQTT connection failed: %s\n",
            mosquitto_strerror(result));
        goto cleanup;
    }

    result = mosquitto_loop_start(mosquitto);

    if (result != MOSQ_ERR_SUCCESS) {
        fprintf(
            stderr,
            "Cannot start MQTT network loop: %s\n",
            mosquitto_strerror(result));
        goto cleanup;
    }

    fprintf(
        stdout,
        "Smart Guard MQTT client started\n"
        "Broker: %s:%d\n"
        "Telemetry topic: %s\n"
        "Persons topic: %s\n"
        "Status/LWT topic: %s\n",
        broker_host,
        broker_port,
        telemetry_topic,
        persons_topic,
        status_topic);

    while (!g_stop) {
        if (g_connected) {
            char *telemetry_json =
                fetch_telemetry(
                    telemetry_url,
                    ca_file);

            if (telemetry_json != NULL) {
                char *persons_json =
                    build_persons_payload(
                        telemetry_json,
                        student_id);

                publish_qos1(
                    mosquitto,
                    telemetry_topic,
                    telemetry_json);

                if (persons_json != NULL) {
                    publish_qos1(
                        mosquitto,
                        persons_topic,
                        persons_json);

                    free(persons_json);
                }

                free(telemetry_json);
            }
        } else {
            fprintf(
                stderr,
                "Waiting for MQTT connection...\n");
        }

        for (int second = 0;
             second < interval_seconds && !g_stop;
             ++second) {
            sleep(1);
        }
    }

    if (g_connected) {
        char *offline_clean_payload =
            build_status_payload(
                student_id,
                "offline-clean");

        if (offline_clean_payload != NULL) {
            mosquitto_publish(
                mosquitto,
                NULL,
                status_topic,
                (int)strlen(offline_clean_payload),
                offline_clean_payload,
                1,
                true);

            free(offline_clean_payload);
        }

        { struct timespec delay = { .tv_sec = 0, .tv_nsec = 200000000L }; (void)nanosleep(&delay, NULL); }
        mosquitto_disconnect(mosquitto);
    }

    mosquitto_loop_stop(mosquitto, false);
    exit_code = EXIT_SUCCESS;

cleanup:
    free(offline_payload);

    if (mosquitto != NULL) {
        mosquitto_destroy(mosquitto);
    }

    mosquitto_lib_cleanup();
    curl_global_cleanup();

    return exit_code;
}
