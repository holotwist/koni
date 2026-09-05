#include "codec.h"
#include <stdlib.h>

#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

static const char* ogg_exts[] = { ".ogg", ".oga", NULL };

struct KoniDecoder {
    stb_vorbis *vorbis;
    KoniAudioFormat fmt;
};

extern bool ogg_read_metadata(const char* filepath, KoniMetadata* meta, uint32_t* duration_sec);

static KoniDecoder* ogg_open(const char* filepath) {
    int error = 0;
    stb_vorbis* vb = stb_vorbis_open_filename(filepath, &error, NULL);
    if (!vb) return NULL;

    stb_vorbis_info info = stb_vorbis_get_info(vb);

    KoniDecoder* dec = calloc(1, sizeof(KoniDecoder));
    if (!dec) {
        stb_vorbis_close(vb);
        return NULL;
    }

    dec->vorbis = vb;
    dec->fmt.sample_rate = info.sample_rate;
    dec->fmt.num_channels = info.channels;
    dec->fmt.bits_per_sample = 32;
    dec->fmt.bitrate = 0;
    dec->fmt.total_samples = stb_vorbis_stream_length_in_samples(vb);

    return dec;
}

static void ogg_close(KoniDecoder* dec) {
    if (dec) {
        if (dec->vorbis) stb_vorbis_close(dec->vorbis);
        free(dec);
    }
}

static bool ogg_get_format(KoniDecoder* dec, KoniAudioFormat* fmt) {
    if (!dec || !fmt) return false;
    *fmt = dec->fmt;
    return true;
}

static uint32_t ogg_decode(KoniDecoder* dec, int32_t* pcm_out_interleaved, uint32_t max_samples) {
    if (!dec || !dec->vorbis || max_samples == 0) return 0;

    int channels = dec->fmt.num_channels;
    short* s16_buf = malloc(sizeof(short) * max_samples * channels);
    if (!s16_buf) return 0;

    int samples_read = stb_vorbis_get_samples_short_interleaved(dec->vorbis, channels, s16_buf, max_samples * channels);

    for (int i = 0; i < samples_read * channels; i++) {
        pcm_out_interleaved[i] = ((int32_t)s16_buf[i]) << 16;
    }

    free(s16_buf);
    return (uint32_t)samples_read;
}

static bool ogg_seek(KoniDecoder* dec, uint64_t target_sample) {
    if (!dec || !dec->vorbis) return false;
    return stb_vorbis_seek_frame(dec->vorbis, (unsigned int)target_sample) != 0;
}

const KoniCodecImpl ogg_codec_impl = {
    .name = "Ogg Vorbis Codec",
    .supported_extensions = ogg_exts,
    .capabilities = KONI_CODEC_CAP_STREAM,
    .open = ogg_open,
    .close = ogg_close,
    .get_format = ogg_get_format,
    .read_metadata = ogg_read_metadata,
    .decode = ogg_decode,
    .seek = ogg_seek,
    .get_interface = NULL,
    .control = NULL
};