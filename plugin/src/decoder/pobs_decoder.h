#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <obs.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>

struct pobs_decoder {
    const AVCodec *codec;
    AVCodecContext *ctx;
    AVPacket *pkt;
    AVFrame *frame;
    bool initialized;
};

bool pobs_decoder_init(struct pobs_decoder *dec);
void pobs_decoder_free(struct pobs_decoder *dec);

// Decodes raw H.264 NAL units / Annex-B chunk and outputs to obs_source_output_video
bool pobs_decoder_decode(struct pobs_decoder *dec,
                         const uint8_t *data,
                         size_t size,
                         obs_source_t *source,
                         uint64_t timestamp_ns);

#ifdef __cplusplus
}
#endif
