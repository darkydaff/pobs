#include "pobs_source.h"
#include "../decoder/pobs_decoder.h"
#include <util/threading.h>
#include <util/bmem.h>
#include <util/platform.h>
#include <obs-module.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define blog(level, msg, ...) blog(level, "pobs-source: " msg, ##__VA_ARGS__)

#define DEFAULT_VIDEO_PORT 8766
#define MAX_PACKET_SIZE (8 * 1024 * 1024)

struct pobs_source {
    obs_source_t *source;
    pthread_t thread;
    os_event_t *stop_event;

    int server_fd;
    int client_fd;
    uint16_t port;

    struct pobs_decoder decoder;

    char *facing;
    char *resolution;
    int fps;

    uint32_t width;
    uint32_t height;
};

static struct pobs_source *s_active_source = NULL;
static long s_current_rotation = 0;

void pobs_source_set_rotation(long rotation) {
    s_current_rotation = rotation;
    if (s_active_source && s_active_source->source) {
        blog(LOG_INFO, "Applying GPU async rotation: %ld degrees", rotation);
        obs_source_set_async_rotation(s_active_source->source, rotation);
    }
}

long pobs_source_get_rotation(void) {
    return s_current_rotation;
}

static bool set_socket_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool read_exact(int fd, uint8_t *buf, size_t len, os_event_t *stop_event) {
    size_t received = 0;
    while (received < len) {
        if (os_event_try(stop_event) != EAGAIN) {
            return false;
        }

        ssize_t n = recv(fd, buf + received, len - received, MSG_WAITALL);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                continue;
            }
            return false;
        }
        received += (size_t)n;
    }
    return true;
}

extern bool pobs_is_stream_active(void);

static uint8_t *s_placeholder_rgba = NULL;
static uint32_t s_placeholder_w = 0;
static uint32_t s_placeholder_h = 0;

static void pobs_render_placeholder_frame(uint32_t width, uint32_t height) {
    if (s_placeholder_rgba && s_placeholder_w == width && s_placeholder_h == height) {
        return;
    }

    if (s_placeholder_rgba) {
        bfree(s_placeholder_rgba);
    }

    s_placeholder_w = width;
    s_placeholder_h = height;
    s_placeholder_rgba = (uint8_t *)bmalloc(width * height * 4);

    uint32_t *pixels = (uint32_t *)s_placeholder_rgba;

    // Background color: #18181b -> R=24, G=24, B=27, A=255 (Little Endian: 0xFF1B1818)
    const uint32_t c_bg = 0xFF1B1818;
    // Card background: #27272a -> R=39, G=39, B=42, A=255
    const uint32_t c_card = 0xFF2A2727;
    // Card border: #3f3f46 -> R=63, G=63, B=70, A=255
    const uint32_t c_border = 0xFF463F3F;
    // Pause bars: #f4f4f5 -> R=244, G=244, B=245, A=255
    const uint32_t c_bar = 0xFFF5F4F4;
    // Status dot (amber paused): #f59e0b -> R=245, G=158, B=11, A=255
    const uint32_t c_amber = 0xFF0B9EF5;

    for (size_t i = 0; i < (size_t)width * height; ++i) {
        pixels[i] = c_bg;
    }

    int32_t cx = (int32_t)width / 2;
    int32_t cy = (int32_t)height / 2;

    int32_t card_w = 260;
    int32_t card_h = 130;
    int32_t x0 = cx - card_w / 2;
    int32_t x1 = cx + card_w / 2;
    int32_t y0 = cy - card_h / 2;
    int32_t y1 = cy + card_h / 2;

    for (int32_t y = y0; y <= y1; ++y) {
        if (y < 0 || y >= (int32_t)height) continue;
        for (int32_t x = x0; x <= x1; ++x) {
            if (x < 0 || x >= (int32_t)width) continue;

            // Rounded rectangle test (radius 16)
            int32_t dx = (x < x0 + 16) ? (x0 + 16 - x) : (x > x1 - 16 ? x - (x1 - 16) : 0);
            int32_t dy = (y < y0 + 16) ? (y0 + 16 - y) : (y > y1 - 16 ? y - (y1 - 16) : 0);
            if (dx * dx + dy * dy > 16 * 16) continue;

            bool is_border = (x == x0 || x == x1 || y == y0 || y == y1 ||
                              (dx * dx + dy * dy >= 14 * 14 && (dx > 0 || dy > 0)));
            pixels[y * width + x] = is_border ? c_border : c_card;
        }
    }

    // Two Pause bars
    int32_t bar_h = 44;
    int32_t b_y0 = cy - 36;
    int32_t b_y1 = b_y0 + bar_h;

    // Bar 1: cx - 20 to cx - 6
    for (int32_t y = b_y0; y <= b_y1; ++y) {
        if (y < 0 || y >= (int32_t)height) continue;
        for (int32_t x = cx - 20; x <= cx - 6; ++x) {
            if (x < 0 || x >= (int32_t)width) continue;
            pixels[y * width + x] = c_bar;
        }
    }
    // Bar 2: cx + 6 to cx + 20
    for (int32_t y = b_y0; y <= b_y1; ++y) {
        if (y < 0 || y >= (int32_t)height) continue;
        for (int32_t x = cx + 6; x <= cx + 20; ++x) {
            if (x < 0 || x >= (int32_t)width) continue;
            pixels[y * width + x] = c_bar;
        }
    }

    // Three sleek amber dots underneath
    int32_t dot_y = cy + 28;
    int32_t dot_xs[3] = {cx - 16, cx, cx + 16};
    for (int d = 0; d < 3; ++d) {
        int32_t dcx = dot_xs[d];
        for (int32_t y = dot_y - 4; y <= dot_y + 4; ++y) {
            if (y < 0 || y >= (int32_t)height) continue;
            for (int32_t x = dcx - 4; x <= dcx + 4; ++x) {
                if (x < 0 || x >= (int32_t)width) continue;
                int32_t r2 = (x - dcx) * (x - dcx) + (y - dot_y) * (y - dot_y);
                if (r2 <= 4 * 4) {
                    pixels[y * width + x] = c_amber;
                }
            }
        }
    }
}

static void pobs_output_placeholder(obs_source_t *source, uint32_t width, uint32_t height) {
    if (!source) return;
    if (width == 0) width = 1920;
    if (height == 0) height = 1080;

    pobs_render_placeholder_frame(width, height);

    struct obs_source_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.format = VIDEO_FORMAT_RGBA;
    frame.width = width;
    frame.height = height;
    frame.timestamp = os_gettime_ns();
    frame.data[0] = s_placeholder_rgba;
    frame.linesize[0] = width * 4;

    video_format_get_parameters_for_format(
        VIDEO_CS_709, VIDEO_RANGE_FULL, VIDEO_FORMAT_RGBA,
        frame.color_matrix, frame.color_range_min, frame.color_range_max);

    obs_source_output_video(source, &frame);
}

static void *pobs_video_thread(void *ptr) {
    struct pobs_source *data = (struct pobs_source *)ptr;
    os_set_thread_name("pobs: video_rx");

    data->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (data->server_fd < 0) {
        blog(LOG_ERROR, "Failed to create video server socket: %s", strerror(errno));
        return NULL;
    }

    int opt = 1;
    setsockopt(data->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_socket_nonblocking(data->server_fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(data->port);

    if (bind(data->server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        blog(LOG_ERROR, "Failed to bind video socket to port %u: %s", data->port, strerror(errno));
        close(data->server_fd);
        data->server_fd = -1;
        return NULL;
    }

    if (listen(data->server_fd, 2) < 0) {
        blog(LOG_ERROR, "Failed to listen on video socket: %s", strerror(errno));
        close(data->server_fd);
        data->server_fd = -1;
        return NULL;
    }

    blog(LOG_INFO, "Listening for companion video stream on 0.0.0.0:%u", data->port);

    uint8_t *packet_buffer = (uint8_t *)bmalloc(MAX_PACKET_SIZE);

    while (os_event_try(data->stop_event) == EAGAIN) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(data->server_fd, &fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000; // 50ms

        int sel = select(data->server_fd + 1, &fds, NULL, NULL, &tv);
        if (sel <= 0) {
            pobs_output_placeholder(data->source, data->width, data->height);
            continue;
        }

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(data->server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            continue;
        }

        // Enable TCP_NODELAY, TCP_QUICKACK and socket receive timeout (64KB buffer prevents TCP bufferbloat lag)
        int nodelay = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
#ifdef TCP_QUICKACK
        int quickack = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_QUICKACK, &quickack, sizeof(quickack));
#endif
        int rcvbuf = 64 * 1024;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
        struct timeval rcv_tv;
        rcv_tv.tv_sec = 1;
        rcv_tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &rcv_tv, sizeof(rcv_tv));

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        blog(LOG_INFO, "Companion video stream connected from %s:%d", client_ip, ntohs(client_addr.sin_port));

        data->client_fd = client_fd;

        // Packet stream loop: [4-byte big-endian length][payload]
        while (os_event_try(data->stop_event) == EAGAIN) {
            // Select with 40ms timeout so we can detect pause or disconnect quickly
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(client_fd, &rfds);
            struct timeval rtv;
            rtv.tv_sec = 0;
            rtv.tv_usec = 40000; // 40ms

            int rsel = select(client_fd + 1, &rfds, NULL, NULL, &rtv);
            if (rsel < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (rsel == 0) {
                if (!pobs_is_stream_active()) {
                    pobs_output_placeholder(data->source, data->width, data->height);
                }
                continue;
            }

            uint8_t header[4];
            if (!read_exact(client_fd, header, 4, data->stop_event)) {
                blog(LOG_INFO, "Companion stream disconnected from %s", client_ip);
                break;
            }

            uint32_t packet_len = ((uint32_t)header[0] << 24) |
                                  ((uint32_t)header[1] << 16) |
                                  ((uint32_t)header[2] << 8)  |
                                  ((uint32_t)header[3]);

            if (packet_len == 0 || packet_len > MAX_PACKET_SIZE) {
                blog(LOG_WARNING, "Invalid packet length received: %u, resetting stream", packet_len);
                break;
            }

            if (!read_exact(client_fd, packet_buffer, packet_len, data->stop_event)) {
                break;
            }

            if (pobs_is_stream_active()) {
                pobs_decoder_decode(&data->decoder, packet_buffer, packet_len, data->source, os_gettime_ns());
                if (data->decoder.frame && data->decoder.frame->width > 0) {
                    data->width = data->decoder.frame->width;
                    data->height = data->decoder.frame->height;
                }
            } else {
                pobs_output_placeholder(data->source, data->width, data->height);
            }
        }

        close(client_fd);
        data->client_fd = -1;
    }


    bfree(packet_buffer);

    if (data->server_fd >= 0) {
        close(data->server_fd);
        data->server_fd = -1;
    }

    return NULL;
}

static const char *pobs_source_get_name(void *unused) {
    (void)unused;
    return "POBS Pixel Camera";
}

static void *pobs_source_create(obs_data_t *settings, obs_source_t *source) {
    struct pobs_source *data = (struct pobs_source *)bzalloc(sizeof(struct pobs_source));
    data->source = source;
    s_active_source = data;
    obs_source_set_async_unbuffered(source, true);
    if (s_current_rotation != 0) {
        blog(LOG_INFO, "Applying initial GPU async rotation: %ld degrees", s_current_rotation);
        obs_source_set_async_rotation(source, s_current_rotation);
    }
    data->port = (uint16_t)obs_data_get_int(settings, "video_port");
    if (data->port == 0) data->port = DEFAULT_VIDEO_PORT;

    data->facing = bstrdup(obs_data_get_string(settings, "camera_facing"));
    if (!data->facing || strlen(data->facing) == 0) {
        bfree(data->facing);
        data->facing = bstrdup("front");
    }

    data->resolution = bstrdup(obs_data_get_string(settings, "resolution"));
    if (!data->resolution || strlen(data->resolution) == 0) {
        bfree(data->resolution);
        data->resolution = bstrdup("1920x1080");
    }

    data->fps = (int)obs_data_get_int(settings, "fps");
    if (data->fps <= 0) data->fps = 60;

    data->server_fd = -1;
    data->client_fd = -1;

    os_event_init(&data->stop_event, OS_EVENT_TYPE_MANUAL);

    if (!pobs_decoder_init(&data->decoder)) {
        blog(LOG_ERROR, "Failed to initialize H.264 video decoder");
    }

    pthread_create(&data->thread, NULL, pobs_video_thread, data);

    return data;
}

static void pobs_source_destroy(void *vptr) {
    struct pobs_source *data = (struct pobs_source *)vptr;
    if (!data) return;

    if (data->stop_event) {
        os_event_signal(data->stop_event);
    }

    if (data->client_fd >= 0) {
        shutdown(data->client_fd, SHUT_RDWR);
        close(data->client_fd);
        data->client_fd = -1;
    }
    if (data->server_fd >= 0) {
        shutdown(data->server_fd, SHUT_RDWR);
        close(data->server_fd);
        data->server_fd = -1;
    }

    pthread_join(data->thread, NULL);

    pobs_decoder_free(&data->decoder);

    if (data->stop_event) {
        os_event_destroy(data->stop_event);
    }

    bfree(data->facing);
    bfree(data->resolution);
    if (s_active_source == data) {
        s_active_source = NULL;
    }
    bfree(data);
}

static void pobs_source_update(void *vptr, obs_data_t *settings) {
    struct pobs_source *data = (struct pobs_source *)vptr;
    if (!data) return;

    const char *facing = obs_data_get_string(settings, "camera_facing");
    if (facing && strlen(facing) > 0) {
        bfree(data->facing);
        data->facing = bstrdup(facing);
    }

    const char *res = obs_data_get_string(settings, "resolution");
    if (res && strlen(res) > 0) {
        bfree(data->resolution);
        data->resolution = bstrdup(res);
    }

    int fps = (int)obs_data_get_int(settings, "fps");
    if (fps > 0) data->fps = fps;
}

static obs_properties_t *pobs_source_get_properties(void *vptr) {
    (void)vptr;
    obs_properties_t *props = obs_properties_create();

    obs_property_t *facing_prop = obs_properties_add_list(
        props, "camera_facing", "Camera Lens", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(facing_prop, "Front Camera", "front");
    obs_property_list_add_string(facing_prop, "Back Camera", "back");

    obs_property_t *res_prop = obs_properties_add_list(
        props, "resolution", "Resolution", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(res_prop, "1920x1080 (Full HD)", "1920x1080");
    obs_property_list_add_string(res_prop, "1280x720 (HD)", "1280x720");
    obs_property_list_add_string(res_prop, "3840x2160 (4K UHD)", "3840x2160");

    obs_property_t *fps_prop = obs_properties_add_list(
        props, "fps", "Framerate", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(fps_prop, "60 FPS", 60);
    obs_property_list_add_int(fps_prop, "30 FPS", 30);

    obs_properties_add_int(props, "video_port", "Video Streaming Port", 1024, 65535, 1);

    return props;
}

static void pobs_source_get_defaults(obs_data_t *settings) {
    obs_data_set_default_string(settings, "camera_facing", "front");
    obs_data_set_default_string(settings, "resolution", "1920x1080");
    obs_data_set_default_int(settings, "fps", 60);
    obs_data_set_default_int(settings, "video_port", DEFAULT_VIDEO_PORT);
}

struct obs_source_info pobs_source_info = {
    .id = "pobs_source",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE,
    .get_name = pobs_source_get_name,
    .create = pobs_source_create,
    .destroy = pobs_source_destroy,
    .update = pobs_source_update,
    .get_properties = pobs_source_get_properties,
    .get_defaults = pobs_source_get_defaults,
    .icon_type = OBS_ICON_TYPE_CAMERA,
};
