#include "codec.h"
#include <stdlib.h>
#include <string.h>
#include "miniaudio.h"

static const char* exts[] = { ".mp3", ".wav", ".flac", NULL };

struct KoniDecoder {
    ma_decoder decoder;
    KoniAudioFormat fmt;
};

static KoniDecoder* ma_open(const char* filepath) {
    KoniDecoder* dec = calloc(1, sizeof(KoniDecoder));
    ma_decoder_config config = ma_decoder_config_init(ma_format_s32, 0, 0);
    if (ma_decoder_init_file(filepath, &config, &dec->decoder) != MA_SUCCESS) {
        free(dec);
        return NULL;
    }
    dec->fmt.sample_rate = dec->decoder.outputSampleRate;
    dec->fmt.num_channels = dec->decoder.outputChannels;
    dec->fmt.bits_per_sample = 32;
    dec->fmt.bitrate = 0;
    
    ma_uint64 length;
    if (ma_decoder_get_length_in_pcm_frames(&dec->decoder, &length) == MA_SUCCESS) {
        dec->fmt.total_samples = length;
    } else {
        dec->fmt.total_samples = 0;
    }
    return dec;
}

static void ma_close(KoniDecoder* dec) {
    if (dec) {
        ma_decoder_uninit(&dec->decoder);
        free(dec);
    }
}

static bool ma_get_fmt(KoniDecoder* dec, KoniAudioFormat* fmt) {
    *fmt = dec->fmt;
    return true;
}

extern bool ma_read_metadata(const char* filepath, KoniMetadata* meta, uint32_t* duration_sec);

static uint32_t ma_decode(KoniDecoder* dec, int32_t* pcm_out, uint32_t max_samples) {
    ma_uint64 framesRead = 0;
    ma_decoder_read_pcm_frames(&dec->decoder, pcm_out, max_samples, &framesRead);
    return (uint32_t)framesRead;
}

static bool ma_seek(KoniDecoder* dec, uint64_t target_sample) {
    return ma_decoder_seek_to_pcm_frame(&dec->decoder, target_sample) == MA_SUCCESS;
}

const KoniCodecImpl ma_codec_impl = {
    .name = "Miniaudio Multi-Format",
    .supported_extensions = exts,
    .open = ma_open,
    .close = ma_close,
    .get_format = ma_get_fmt,
    .read_metadata = ma_read_metadata,
    .decode = ma_decode,
    .seek = ma_seek
};