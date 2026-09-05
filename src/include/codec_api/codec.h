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

// Codec Capabilities Bitmask
typedef enum {
    KONI_CODEC_CAP_STREAM          = (1 << 0), /* Standard streamed audio */
    KONI_CODEC_CAP_TRACKER         = (1 << 1), /* Pattern/Row/Channel based */
    KONI_CODEC_CAP_SYNTH           = (1 << 2), /* Real-time synthesized chiptune/engine */
    KONI_CODEC_CAP_CHANNEL_CONTROL = (1 << 3), /* Can mute/solo/pan internal channels */
    KONI_CODEC_CAP_EXTENDED_TAGS   = (1 << 4)  /* Exposes format-specific extra metadata */
} KoniCodecCapability;

// Interface Query Identifiers
typedef enum {
    KONI_IFACE_TRACKER  = 1,
    KONI_IFACE_CHANNELS = 2,
    KONI_IFACE_CUSTOM   = 1000
} KoniInterfaceID;

// Opaque decoder instance
typedef struct KoniDecoder KoniDecoder;

// Tracker Interface Contract (for PxTone-like codecs)
typedef struct {
    uint32_t    (*get_num_channels)(KoniDecoder *dec);
    const char* (*get_channel_name)(KoniDecoder *dec, uint32_t channel_idx);
    uint32_t    (*get_current_row)(KoniDecoder *dec);
    uint32_t    (*get_total_rows)(KoniDecoder *dec);
    uint32_t    (*get_bpm)(KoniDecoder *dec);
    /* Retrieves cell contents for a channel at a given row */
    bool        (*get_cell_data)(KoniDecoder *dec, uint32_t row, uint32_t channel_idx,
                                 char out_note[4], uint8_t *out_instrument, uint8_t *out_volume);
} KoniTrackerInterface;

typedef struct {
    const char* name;
    const char** supported_extensions; // NULL-terminated array
    uint32_t capabilities;             // Bitmask of KoniCodecCapability
    
    KoniDecoder* (*open)(const char* filepath);
    void (*close)(KoniDecoder* dec);
    
    bool (*get_format)(KoniDecoder* dec, KoniAudioFormat* fmt);
    bool (*read_metadata)(const char* filepath, KoniMetadata* meta, uint32_t* duration_sec);
    
    // Decodes into interleaved format. Returns number of samples generated per channel.
    uint32_t (*decode)(KoniDecoder* dec, int32_t* pcm_out_interleaved, uint32_t max_samples);
    bool (*seek)(KoniDecoder* dec, uint64_t target_sample);

    /* Optional Extension Hooks */
    void* (*get_interface)(KoniDecoder *dec, KoniInterfaceID iface_id);
    int   (*control)(KoniDecoder *dec, int cmd_id, void *arg_in, void *arg_out);
} KoniCodecImpl;

// Global Registry
void koni_codecs_init(void);
const KoniCodecImpl* koni_find_codec_by_ext(const char* filepath);
bool koni_is_supported_extension(const char* ext);
void koni_metadata_free(KoniMetadata* meta);

#endif // KONI_CODEC_H