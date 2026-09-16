#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "opus_codec.h"
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
  #if __has_include(<opus/opusfile.h>)
    #include <opus/opusfile.h>
  #elif __has_include(<opusfile.h>)
    #include <opusfile.h>
  #else
    #include <opusfile.h>
  #endif
#else
  #include <opusfile.h>
#endif

static const char* opus_exts[] = { ".opus", NULL };

struct KoniDecoder {
    OggOpusFile *of;
    KoniAudioFormat fmt;
};

static KoniDecoder* opus_codec_open(const char *filepath) {
    if (!filepath || !filepath[0]) return NULL;

    int err = 0;
    OggOpusFile *of = op_open_file(filepath, &err);
    if (!of) return NULL;

    KoniDecoder *dec = calloc(1, sizeof(KoniDecoder));
    if (!dec) {
        op_free(of);
        return NULL;
    }

    dec->of = of;
    dec->fmt.sample_rate = 48000;
    dec->fmt.num_channels = (uint16_t)op_channel_count(of, -1);
    dec->fmt.bits_per_sample = 32;
    dec->fmt.bitrate = (uint32_t)op_bitrate(of, -1);

    ogg_int64_t total = op_pcm_total(of, -1);
    dec->fmt.total_samples = total > 0 ? (uint64_t)total : 0;

    return dec;
}

static void opus_codec_close(KoniDecoder *dec) {
    if (!dec) return;
    if (dec->of) op_free(dec->of);
    free(dec);
}

static bool opus_codec_get_format(KoniDecoder *dec, KoniAudioFormat *fmt) {
    if (!dec || !fmt) return false;
    *fmt = dec->fmt;
    return true;
}

static uint32_t opus_codec_decode(KoniDecoder *dec, int32_t *pcm_out_interleaved, uint32_t max_samples) {
    if (!dec || !dec->of || !pcm_out_interleaved || max_samples == 0) return 0;

    uint32_t channels = dec->fmt.num_channels > 0 ? dec->fmt.num_channels : 2;
    int16_t s16_buf[4096];
    uint32_t frames_produced = 0;

    while (frames_produced < max_samples) {
        uint32_t needed_frames = max_samples - frames_produced;
        uint32_t chunk_frames = (sizeof(s16_buf) / sizeof(int16_t)) / channels;
        if (chunk_frames > needed_frames) chunk_frames = needed_frames;

        int read_frames = op_read(dec->of, s16_buf, (int)(chunk_frames * channels), NULL);
        if (read_frames <= 0) break;

        for (int i = 0; i < read_frames * (int)channels; i++) {
            pcm_out_interleaved[frames_produced * channels + i] = ((int32_t)s16_buf[i]) << 16;
        }

        frames_produced += (uint32_t)read_frames;
    }

    return frames_produced;
}

static bool opus_codec_seek(KoniDecoder *dec, uint64_t target_sample) {
    if (!dec || !dec->of) return false;
    return op_pcm_seek(dec->of, (ogg_int64_t)target_sample) == 0;
}

const KoniCodecImpl opus_codec_impl = {
    .name = "Opus (libopusfile)",
    .supported_extensions = opus_exts,
    .capabilities = KONI_CODEC_CAP_STREAM,
    .open = opus_codec_open,
    .close = opus_codec_close,
    .get_format = opus_codec_get_format,
    .read_metadata = opus_read_metadata,
    .decode = opus_codec_decode,
    .seek = opus_codec_seek,
    .get_interface = NULL,
    .control = NULL
};
REGISTER_KONI_CODEC(opus_codec_impl);