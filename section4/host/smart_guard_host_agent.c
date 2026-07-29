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
#define DEFAULT_CAMERA_WIDTH 640U
#define DEFAULT_CAMERA_HEIGHT 480U
#define DEFAULT_CAMERA_FPS 10U
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
static unsigned int g_camera_width = DEFAULT_CAMERA_WIDTH;
static unsigned int g_camera_height = DEFAULT_CAMERA_HEIGHT;
static unsigned int g_camera_fps = DEFAULT_CAMERA_FPS;

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

static unsigned int get_env_uint(
    const char *name,
    unsigned int fallback,
    unsigned int minimum,
    unsigned int maximum)
{
    const char *value = getenv(name);
    char *end = NULL;
    unsigned long parsed;

    if (value == NULL || value[0] == '\0') {
        return fallback;
    }

    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' ||
        parsed < minimum || parsed > maximum) {
        fprintf(stderr, "Invalid %s=%s; using %u\n", name, value, fallback);
        return fallback;
    }

    return (unsigned int)parsed;
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
    format.fmt.pix.width = g_camera_width;
    format.fmt.pix.height = g_camera_height;
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
    stream_parameters.parm.capture.timeperframe.denominator = g_camera_fps;
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

    g_camera_width = get_env_uint(
        "SMART_GUARD_CAMERA_WIDTH",
        DEFAULT_CAMERA_WIDTH,
        160U,
        3840U
    );

    g_camera_height = get_env_uint(
        "SMART_GUARD_CAMERA_HEIGHT",
        DEFAULT_CAMERA_HEIGHT,
        120U,
        2160U
    );

    g_camera_fps = get_env_uint(
        "SMART_GUARD_CAMERA_FPS",
        DEFAULT_CAMERA_FPS,
        1U,
        60U
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
        "Host agent started: vm=%s temp_port=%d camera_port=%d video=%s requested=%ux%u@%uFPS\n",
        g_vm_ip,
        g_temp_port,
        g_camera_port,
        g_video_device,
        g_camera_width,
        g_camera_height,
        g_camera_fps
    );

    pthread_join(temperature_thread, NULL);
    pthread_join(camera_thread, NULL);

    fprintf(stderr, "Host agent stopped cleanly\n");
    return EXIT_SUCCESS;
}
