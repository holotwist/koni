#ifndef KONI_CODEC_H
#define KONI_CODEC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint32_t sample_rate;
    uint16_t num_channels;
    uint16_t bits_per_sample;
    uint32_t bitrate; // in bps
    uint64_t total_samples;
} KoniAudioFormat;

typedef struct {
    char *title;
    char *artist;
    char *album;
    char *lyrics;
    char *art_url;
    bool has_track_gain;
    float track_gain; // In decibels (dB)
} KoniMetadata;

// Opaque decoder instance
typedef struct KoniDecoder KoniDecoder;

typedef struct {
    const char* name;
    const char** supported_extensions; // NULL-terminated array
    
    KoniDecoder* (*open)(const char* filepath);
    void (*close)(KoniDecoder* dec);
    
    bool (*get_format)(KoniDecoder* dec, KoniAudioFormat* fmt);
    bool (*read_metadata)(const char* filepath, KoniMetadata* meta, uint32_t* duration_sec);
    
    // Decodes into interleaved format. Returns number of samples generated per channel.
    uint32_t (*decode)(KoniDecoder* dec, int32_t* pcm_out_interleaved, uint32_t max_samples);
    bool (*seek)(KoniDecoder* dec, uint64_t target_sample);
} KoniCodecImpl;

// Global Registry
void koni_codecs_init(void);
const KoniCodecImpl* koni_find_codec_by_ext(const char* filepath);
bool koni_is_supported_extension(const char* ext);
void koni_metadata_free(KoniMetadata* meta);

#endif // KONI_CODEC_H