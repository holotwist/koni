#include "codec.h"
#include "pxtn_tiny.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char* pxtn_exts[] = { ".ptcop", ".pttune", NULL };

struct KoniDecoder {
    PxtnTiny *pxtn;
    KoniAudioFormat fmt;
};

static KoniDecoder* pxtn_codec_open(const char* filepath) {
    uint32_t sample_rate = 44100;
    PxtnTiny *pt = pxtn_tiny_open(filepath, sample_rate);
    if (!pt) return NULL;

    PxtnSongInfo info;
    pxtn_tiny_get_info(pt, &info);

    KoniDecoder *dec = calloc(1, sizeof(KoniDecoder));
    dec->pxtn = pt;
    dec->fmt.sample_rate = sample_rate;
    dec->fmt.num_channels = 2;
    dec->fmt.bits_per_sample = 32;
    dec->fmt.bitrate = sample_rate * 2 * 32;
    dec->fmt.total_samples = info.total_samples;

    return dec;
}

static void pxtn_codec_close(KoniDecoder* dec) {
    if (!dec) return;
    if (dec->pxtn) pxtn_tiny_close(dec->pxtn);
    free(dec);
}

static bool pxtn_codec_get_format(KoniDecoder* dec, KoniAudioFormat* fmt) {
    if (!dec || !fmt) return false;
    *fmt = dec->fmt;
    return true;
}

static bool pxtn_codec_read_metadata(const char* filepath, KoniMetadata* meta, uint32_t* duration_sec) {
    memset(meta, 0, sizeof(KoniMetadata));
    if (duration_sec) *duration_sec = 0;

    PxtnTiny *pt = pxtn_tiny_open(filepath, 44100);
    if (!pt) return false;

    PxtnSongInfo info;
    pxtn_tiny_get_info(pt, &info);

    if (info.name[0] != '\0') {
        meta->title = strdup(info.name);
    }
    if (info.comment[0] != '\0') {
        meta->lyrics = strdup(info.comment);
    }

    meta->has_track_gain = false;
    meta->track_gain = 0.0f;

    if (duration_sec) *duration_sec = info.duration_sec;

    pxtn_tiny_close(pt);
    return true;
}

static uint32_t pxtn_codec_decode(KoniDecoder* dec, int32_t* pcm_out_interleaved, uint32_t max_samples) {
    if (!dec || !dec->pxtn) return 0;
    return pxtn_tiny_render(dec->pxtn, pcm_out_interleaved, max_samples);
}

static bool pxtn_codec_seek(KoniDecoder* dec, uint64_t target_sample) {
    if (!dec || !dec->pxtn) return false;
    return pxtn_tiny_seek(dec->pxtn, target_sample);
}

const KoniCodecImpl pxtone_codec_impl = {
    .name = "PxTone Audio (.ptcop, .pttune)",
    .supported_extensions = pxtn_exts,
    .open = pxtn_codec_open,
    .close = pxtn_codec_close,
    .get_format = pxtn_codec_get_format,
    .read_metadata = pxtn_codec_read_metadata,
    .decode = pxtn_codec_decode,
    .seek = pxtn_codec_seek
};