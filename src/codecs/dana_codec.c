#include "codec.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>

#ifdef ENABLE_DANA
#include "DANADecoder.h"
#include "DANAUtility.h"

#define BUFFER_CHUNK_SIZE 16384

typedef struct {
    FILE *fp;
    struct DANAStreamingDecoder *decoder;
    uint8_t *pending_chunk;
    size_t pending_bytes;
    int pending_append;
    struct { uint8_t *ptr; size_t size; size_t consumed; } allocs[256];
    int alloc_head, alloc_tail;
    size_t total_bytes_read, total_consumed_bytes;
    DANAApiResult last_res;
    struct DANAHeaderInfo header;
    uint32_t full_header_size;
} StreamState;

struct KoniDecoder {
    StreamState ss1;
    StreamState ss2;
    int hybrid_mode;
    
    int32_t **pcm_out1;
    int32_t **pcm_out2;
    int32_t **q1;
    int32_t **q2;
    uint32_t q1_len;
    uint32_t q2_len;

    struct DANAStreamingDecoderConfig cfg;
    
    KoniAudioFormat fmt;
    KoniMetadata meta;
    
    uint32_t samples_played;
};

static void init_stream_state(StreamState *ss, FILE *fp) {
    memset(ss, 0, sizeof(StreamState));
    ss->fp = fp;
    ss->pending_append = 1;
}

static void free_stream_state(StreamState *ss) {
    if (ss->decoder) DANAStreamingDecoder_Destroy(ss->decoder);
    while (ss->alloc_head != ss->alloc_tail) { free(ss->allocs[ss->alloc_head].ptr); ss->alloc_head = (ss->alloc_head + 1) % 256; }
    if (ss->pending_chunk) free(ss->pending_chunk);
    if (ss->fp) fclose(ss->fp);
    DANAMetadata_Release(&ss->header.metadata);
}

static void feed_and_decode(StreamState *ss, int32_t **pcm_out, uint32_t out_max, uint32_t *out_samples) {
    uint8_t dummy_byte = 0;
    DANAStreamingDecoder_AppendDataFragment(ss->decoder, &dummy_byte, 0);

    while (ss->pending_append) {
        if (((ss->alloc_tail + 1) % 256) == ss->alloc_head) break;
        uint8_t *chunk; size_t bytes;
        if (ss->pending_chunk) {
            chunk = ss->pending_chunk; bytes = ss->pending_bytes; ss->pending_chunk = NULL;
        } else {
            chunk = malloc(BUFFER_CHUNK_SIZE);
            if (!chunk) break;
            bytes = fread(chunk, 1, BUFFER_CHUNK_SIZE, ss->fp);
            if (bytes == 0) { free(chunk); ss->pending_append = 0; break; }
            ss->total_bytes_read += bytes;
        }
        if (DANAStreamingDecoder_AppendDataFragment(ss->decoder, chunk, (uint32_t)bytes) != DANA_APIRESULT_OK) {
            ss->pending_chunk = chunk; ss->pending_bytes = bytes; break;
        }
        ss->allocs[ss->alloc_tail].ptr = chunk; ss->allocs[ss->alloc_tail].size = bytes; ss->allocs[ss->alloc_tail].consumed = 0;
        ss->alloc_tail = (ss->alloc_tail + 1) % 256;
    }

    ss->last_res = DANAStreamingDecoder_Decode(ss->decoder, pcm_out, out_max, out_samples);

    const uint8_t *consumed_ptr; uint32_t consumed_size;
    while (DANAStreamingDecoder_CollectDataFragment(ss->decoder, &consumed_ptr, &consumed_size) == DANA_APIRESULT_OK) {
        ss->total_consumed_bytes += consumed_size;
        if (consumed_size > 0 && ss->alloc_head != ss->alloc_tail) {
            ss->allocs[ss->alloc_head].consumed += consumed_size;
            if (ss->allocs[ss->alloc_head].consumed == ss->allocs[ss->alloc_head].size) {
                free(ss->allocs[ss->alloc_head].ptr); ss->alloc_head = (ss->alloc_head + 1) % 256;
            }
        }
    }
}

static KoniDecoder* dana_open(const char* filepath) {
    KoniDecoder* dec = calloc(1, sizeof(KoniDecoder));
    init_stream_state(&dec->ss1, NULL);
    init_stream_state(&dec->ss2, NULL);

    dec->ss1.fp = fopen(filepath, "rb");
    if (!dec->ss1.fp) { free(dec); return NULL; }

    dec->hybrid_mode = 0;
    char dahc_filepath[1024];
    size_t len = strlen(filepath);
    if (len > 5 && strcasecmp(filepath + len - 5, ".dahl") == 0) {
        strcpy(dahc_filepath, filepath);
        strcpy(dahc_filepath + len - 5, ".dahc");
        dec->ss2.fp = fopen(dahc_filepath, "rb");
        if (dec->ss2.fp) dec->hybrid_mode = 1;
    }

    uint8_t header_buf[43];
    if (fread(header_buf, 1, 43, dec->ss1.fp) < 43) { 
        free_stream_state(&dec->ss1); if(dec->hybrid_mode) free_stream_state(&dec->ss2); free(dec); return NULL; 
    }
    uint32_t offset = (((uint32_t)header_buf[4] << 24) | ((uint32_t)header_buf[5] << 16) | ((uint32_t)header_buf[6] << 8) | header_buf[7]);
    dec->ss1.full_header_size = offset + 8;
    
    uint8_t* full_header_buf = malloc(dec->ss1.full_header_size);
    fseek(dec->ss1.fp, 0, SEEK_SET);
    if (fread(full_header_buf, 1, dec->ss1.full_header_size, dec->ss1.fp) < dec->ss1.full_header_size) {
        free(full_header_buf); free_stream_state(&dec->ss1); if(dec->hybrid_mode) free_stream_state(&dec->ss2); free(dec); return NULL;
    }

    if (DANADecoder_DecodeHeader(full_header_buf, dec->ss1.full_header_size, &dec->ss1.header, NULL) != DANA_APIRESULT_OK) {
        free(full_header_buf); free_stream_state(&dec->ss1); if(dec->hybrid_mode) free_stream_state(&dec->ss2); free(dec); return NULL;
    }
    free(full_header_buf);

    if (dec->hybrid_mode) {
        if (fread(header_buf, 1, 43, dec->ss2.fp) < 43) dec->hybrid_mode = 0;
        else {
            uint32_t offset2 = (((uint32_t)header_buf[4] << 24) | ((uint32_t)header_buf[5] << 16) | ((uint32_t)header_buf[6] << 8) | header_buf[7]);
            dec->ss2.full_header_size = offset2 + 8;
            uint8_t* full_header_buf2 = malloc(dec->ss2.full_header_size);
            fseek(dec->ss2.fp, 0, SEEK_SET);
            if (fread(full_header_buf2, 1, dec->ss2.full_header_size, dec->ss2.fp) < dec->ss2.full_header_size) dec->hybrid_mode = 0;
            else if (DANADecoder_DecodeHeader(full_header_buf2, dec->ss2.full_header_size, &dec->ss2.header, NULL) != DANA_APIRESULT_OK) dec->hybrid_mode = 0;
            free(full_header_buf2);
        }
    }

    dec->cfg.core_config.max_num_channels = 8;
    dec->cfg.core_config.max_num_block_samples = 16384;
    dec->cfg.core_config.max_parcor_order = 48;
    dec->cfg.core_config.max_longterm_order = 5;
    dec->cfg.core_config.max_lms_order_per_filter = 40;
    dec->cfg.core_config.enable_crc_check = 1;
    dec->cfg.core_config.verpose_flag = 0;
    dec->cfg.decode_interval_hz = 120.0f;
    dec->cfg.max_bit_per_sample = 32;

    dec->ss1.decoder = DANAStreamingDecoder_Create(&dec->cfg);
    DANAStreamingDecoder_SetWaveFormat(dec->ss1.decoder, &dec->ss1.header.wave_format);
    DANAStreamingDecoder_SetEncodeParameter(dec->ss1.decoder, &dec->ss1.header.encode_param);

    if (dec->hybrid_mode) {
        dec->ss2.decoder = DANAStreamingDecoder_Create(&dec->cfg);
        DANAStreamingDecoder_SetWaveFormat(dec->ss2.decoder, &dec->ss2.header.wave_format);
        DANAStreamingDecoder_SetEncodeParameter(dec->ss2.decoder, &dec->ss2.header.encode_param);
    }

    dec->pcm_out1 = malloc(sizeof(int32_t*) * dec->ss1.header.wave_format.num_channels);
    dec->pcm_out2 = dec->hybrid_mode ? malloc(sizeof(int32_t*) * dec->ss1.header.wave_format.num_channels) : NULL;
    dec->q1 = malloc(sizeof(int32_t*) * dec->ss1.header.wave_format.num_channels);
    dec->q2 = dec->hybrid_mode ? malloc(sizeof(int32_t*) * dec->ss1.header.wave_format.num_channels) : NULL;
    
    for(uint32_t i=0; i < dec->ss1.header.wave_format.num_channels; i++) {
        dec->pcm_out1[i] = malloc(sizeof(int32_t) * 16384);
        dec->q1[i] = malloc(sizeof(int32_t) * 131072);
        if (dec->hybrid_mode) {
            dec->pcm_out2[i] = malloc(sizeof(int32_t) * 16384);
            dec->q2[i] = malloc(sizeof(int32_t) * 131072);
        }
    }

    dec->fmt.sample_rate = dec->ss1.header.wave_format.sampling_rate;
    dec->fmt.num_channels = dec->ss1.header.wave_format.num_channels;
    dec->fmt.bits_per_sample = dec->ss1.header.wave_format.bit_per_sample;
    dec->fmt.bitrate = dec->ss1.header.max_bit_per_second;
    dec->fmt.total_samples = dec->ss1.header.num_samples;

    if (dec->ss1.header.metadata.title) dec->meta.title = strdup((char*)dec->ss1.header.metadata.title);
    if (dec->ss1.header.metadata.artist) dec->meta.artist = strdup((char*)dec->ss1.header.metadata.artist);
    if (dec->ss1.header.metadata.album) dec->meta.album = strdup((char*)dec->ss1.header.metadata.album);
    if (dec->ss1.header.metadata.lyrics) dec->meta.lyrics = strdup((char*)dec->ss1.header.metadata.lyrics);

    return dec;
}

static void dana_close(KoniDecoder* dec) {
    if (!dec) return;
    for(uint32_t i=0; i < dec->ss1.header.wave_format.num_channels; i++) {
        free(dec->pcm_out1[i]); free(dec->q1[i]);
        if (dec->hybrid_mode) { free(dec->pcm_out2[i]); free(dec->q2[i]); }
    }
    free(dec->pcm_out1); free(dec->q1);
    if (dec->hybrid_mode) { free(dec->pcm_out2); free(dec->q2); }
    
    free_stream_state(&dec->ss1);
    if (dec->hybrid_mode) free_stream_state(&dec->ss2);

    if (dec->meta.title) free(dec->meta.title);
    if (dec->meta.artist) free(dec->meta.artist);
    if (dec->meta.album) free(dec->meta.album);
    if (dec->meta.lyrics) free(dec->meta.lyrics);

    free(dec);
}

static bool dana_get_fmt(KoniDecoder* dec, KoniAudioFormat* fmt) {
    *fmt = dec->fmt; return true;
}

static bool dana_get_meta(KoniDecoder* dec, KoniMetadata* meta) {
    if (dec->meta.title) meta->title = strdup(dec->meta.title);
    if (dec->meta.artist) meta->artist = strdup(dec->meta.artist);
    if (dec->meta.album) meta->album = strdup(dec->meta.album);
    if (dec->meta.lyrics) meta->lyrics = strdup(dec->meta.lyrics);
    return true;
}

static uint32_t dana_decode(KoniDecoder* dec, int32_t* pcm_out, uint32_t max_samples) {
    uint32_t out_max = 16384;
    
    while (dec->q1_len < max_samples) {
        uint32_t out_samples1 = 0;
        feed_and_decode(&dec->ss1, dec->pcm_out1, out_max, &out_samples1);
        if (out_samples1 > 0) {
            for(uint32_t c=0; c < dec->ss1.header.wave_format.num_channels; c++) 
                memcpy(dec->q1[c] + dec->q1_len, dec->pcm_out1[c], out_samples1 * sizeof(int32_t));
            dec->q1_len += out_samples1;
        }

        if (dec->hybrid_mode) {
            uint32_t out_samples2 = 0;
            feed_and_decode(&dec->ss2, dec->pcm_out2, out_max, &out_samples2);
            if (out_samples2 > 0) {
                for(uint32_t c=0; c < dec->ss1.header.wave_format.num_channels; c++) 
                    memcpy(dec->q2[c] + dec->q2_len, dec->pcm_out2[c], out_samples2 * sizeof(int32_t));
                dec->q2_len += out_samples2;
            }
        }

        if (!dec->ss1.pending_append && out_samples1 == 0) {
            uint32_t remain; 
            DANAStreamingDecoder_GetRemainDataSize(dec->ss1.decoder, &remain);
            if (remain == 0 || dec->ss1.last_res == DANA_APIRESULT_INSUFFICIENT_DATA_SIZE) break;
        }
    }

    uint32_t mix_samples = dec->hybrid_mode ? (dec->q1_len < dec->q2_len ? dec->q1_len : dec->q2_len) : dec->q1_len;
    if (mix_samples > max_samples) mix_samples = max_samples;
    if (mix_samples == 0) return 0;

    for (uint32_t s = 0; s < mix_samples; s++) {
        for (uint32_t c = 0; c < dec->ss1.header.wave_format.num_channels; c++) {
            int32_t v1 = dec->q1[c][s];
            int32_t v2 = dec->hybrid_mode ? dec->q2[c][s] : 0;
            long long mixed = (long long)v1 + v2;
            if (mixed > 2147483647LL) mixed = 2147483647LL;
            else if (mixed < -2147483648LL) mixed = -2147483648LL;
            pcm_out[s * dec->ss1.header.wave_format.num_channels + c] = (int32_t)mixed;
        }
    }

    for (uint32_t c = 0; c < dec->ss1.header.wave_format.num_channels; c++) {
        memmove(dec->q1[c], dec->q1[c] + mix_samples, (dec->q1_len - mix_samples) * sizeof(int32_t));
        if (dec->hybrid_mode) memmove(dec->q2[c], dec->q2[c] + mix_samples, (dec->q2_len - mix_samples) * sizeof(int32_t));
    }
    dec->q1_len -= mix_samples;
    if (dec->hybrid_mode) dec->q2_len -= mix_samples;

    dec->samples_played += mix_samples;
    return mix_samples;
}

static bool dana_seek(KoniDecoder* dec, uint64_t target_sample) {
    if (!dec->ss1.header.metadata.seek_table) return false;
    uint32_t out_sample = 0, out_byte_offset = 0;
    if (DANADecoder_GetSeekPoint(&dec->ss1.header.metadata, (uint32_t)target_sample, &out_sample, &out_byte_offset) == DANA_APIRESULT_OK) {
        fseek(dec->ss1.fp, dec->ss1.full_header_size + out_byte_offset, SEEK_SET);
        if (dec->hybrid_mode && dec->ss2.fp) fseek(dec->ss2.fp, dec->ss2.full_header_size + out_byte_offset, SEEK_SET);
        
        DANAStreamingDecoder_Destroy(dec->ss1.decoder);
        dec->ss1.decoder = DANAStreamingDecoder_Create(&dec->cfg);
        DANAStreamingDecoder_SetWaveFormat(dec->ss1.decoder, &dec->ss1.header.wave_format);
        DANAStreamingDecoder_SetEncodeParameter(dec->ss1.decoder, &dec->ss1.header.encode_param);
        while (dec->ss1.alloc_head != dec->ss1.alloc_tail) { free(dec->ss1.allocs[dec->ss1.alloc_head].ptr); dec->ss1.alloc_head = (dec->ss1.alloc_head + 1) % 256; }
        if (dec->ss1.pending_chunk) { free(dec->ss1.pending_chunk); dec->ss1.pending_chunk = NULL; }
        dec->ss1.pending_append = 1;

        if (dec->hybrid_mode) {
            DANAStreamingDecoder_Destroy(dec->ss2.decoder);
            dec->ss2.decoder = DANAStreamingDecoder_Create(&dec->cfg);
            DANAStreamingDecoder_SetWaveFormat(dec->ss2.decoder, &dec->ss2.header.wave_format);
            DANAStreamingDecoder_SetEncodeParameter(dec->ss2.decoder, &dec->ss2.header.encode_param);
            while (dec->ss2.alloc_head != dec->ss2.alloc_tail) { free(dec->ss2.allocs[dec->ss2.alloc_head].ptr); dec->ss2.alloc_head = (dec->ss2.alloc_head + 1) % 256; }
            if (dec->ss2.pending_chunk) { free(dec->ss2.pending_chunk); dec->ss2.pending_chunk = NULL; }
            dec->ss2.pending_append = 1;
        }

        dec->q1_len = 0;
        if (dec->hybrid_mode) dec->q2_len = 0;
        dec->samples_played = out_sample;
        return true;
    }
    return false;
}

static const char* exts[] = { ".dana", ".dahl", NULL };

const KoniCodecImpl dana_codec_impl = {
    .name = "DANA Audio",
    .supported_extensions = exts,
    .open = dana_open,
    .close = dana_close,
    .get_format = dana_get_fmt,
    .get_metadata = dana_get_meta,
    .decode = dana_decode,
    .seek = dana_seek
};

#else

#include <stddef.h>

static const char* exts[] = { NULL };

static KoniDecoder* dummy_open(const char* filepath) {
    (void)filepath;
    return NULL;
}

const KoniCodecImpl dana_codec_impl = {
    .name = "DANA Audio (Disabled)",
    .supported_extensions = exts,
    .open = dummy_open,
    .close = NULL,
    .get_format = NULL,
    .get_metadata = NULL,
    .decode = NULL,
    .seek = NULL
};

#endif