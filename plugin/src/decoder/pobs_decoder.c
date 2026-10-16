#include "pobs_decoder.h"
#include <util/bmem.h>
#include <util/platform.h>
#include <obs-module.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>

#define blog(level, msg, ...) blog(level, "pobs-decoder: " msg, ##__VA_ARGS__)

bool pobs_decoder_init(struct pobs_decoder *dec) {
    if (!dec) return false;
    memset(dec, 0, sizeof(*dec));

    dec->codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!dec->codec) {
        blog(LOG_ERROR, "Failed to find FFmpeg H.264 decoder");
        return false;
    }

    dec->ctx = avcodec_alloc_context3(dec->codec);
    if (!dec->ctx) {
        blog(LOG_ERROR, "Failed to allocate AVCodecContext");
        return false;
    }

    // Zero-latency decoding optimizations
    dec->ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    dec->ctx->flags2 |= AV_CODEC_FLAG2_FAST;
    dec->ctx->delay = 0;
    dec->ctx->max_b_frames = 0;
    dec->ctx->has_b_frames = 0;
    dec->ctx->thread_type = FF_THREAD_SLICE; // Parallel slice decoding (0 frame delay)
    dec->ctx->thread_count = 4;

    AVDictionary *opts = NULL;
    av_dict_set(&opts, "tune", "zerolatency", 0);
    av_dict_set(&opts, "flags", "low_delay", 0);

    int open_res = avcodec_open2(dec->ctx, dec->codec, &opts);
    av_dict_free(&opts);

    if (open_res < 0) {
        blog(LOG_ERROR, "Failed to open AVCodecContext for H.264");
        avcodec_free_context(&dec->ctx);
        return false;
    }

    dec->pkt = av_packet_alloc();
    dec->frame = av_frame_alloc();
    if (!dec->pkt || !dec->frame) {
        pobs_decoder_free(dec);
        return false;
    }

    dec->initialized = true;
    blog(LOG_INFO, "FFmpeg H.264 decoder initialized successfully");
    return true;
}

void pobs_decoder_free(struct pobs_decoder *dec) {
    if (!dec) return;
    if (dec->frame) {
        av_frame_free(&dec->frame);
    }
    if (dec->pkt) {
        av_packet_free(&dec->pkt);
    }
    if (dec->ctx) {
        avcodec_free_context(&dec->ctx);
    }
    dec->initialized = false;
}

static enum video_format av_to_obs_format(enum AVPixelFormat av_fmt) {
    switch (av_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
        return VIDEO_FORMAT_I420;
    case AV_PIX_FMT_NV12:
        return VIDEO_FORMAT_NV12;
    case AV_PIX_FMT_YUYV422:
        return VIDEO_FORMAT_YUY2;
    case AV_PIX_FMT_UYVY422:
        return VIDEO_FORMAT_UYVY;
    case AV_PIX_FMT_RGBA:
        return VIDEO_FORMAT_RGBA;
    case AV_PIX_FMT_BGRA:
        return VIDEO_FORMAT_BGRA;
    default:
        return VIDEO_FORMAT_I420;
    }
}

bool pobs_decoder_decode(struct pobs_decoder *dec,
                         const uint8_t *data,
                         size_t size,
                         obs_source_t *source,
                         uint64_t timestamp_ns) {
    if (!dec || !dec->initialized || !data || size == 0 || !source) {
        return false;
    }

    dec->pkt->data = (uint8_t *)data;
    dec->pkt->size = (int)size;

    int ret = avcodec_send_packet(dec->ctx, dec->pkt);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        return false;
    }

    bool output_any = false;
    while (avcodec_receive_frame(dec->ctx, dec->frame) == 0) {
        struct obs_source_frame obs_frame;
        memset(&obs_frame, 0, sizeof(obs_frame));

        enum video_format vf = av_to_obs_format((enum AVPixelFormat)dec->frame->format);
        obs_frame.format = vf;
        obs_frame.width = dec->frame->width;
        obs_frame.height = dec->frame->height;
        obs_frame.timestamp = (timestamp_ns > 0) ? timestamp_ns : os_gettime_ns();

        for (int i = 0; i < 3; i++) {
            obs_frame.data[i] = dec->frame->data[i];
            obs_frame.linesize[i] = dec->frame->linesize[i];
        }

        enum video_range_type vrange = (dec->frame->color_range == AVCOL_RANGE_JPEG)
                                           ? VIDEO_RANGE_FULL
                                           : VIDEO_RANGE_PARTIAL;
        video_format_get_parameters_for_format(VIDEO_CS_709, vrange, vf,
                                               obs_frame.color_matrix,
                                               obs_frame.color_range_min,
                                               obs_frame.color_range_max);

        obs_source_output_video(source, &obs_frame);
        output_any = true;
    }

    return output_any;
}
