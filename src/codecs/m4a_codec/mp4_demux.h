#ifndef MP4_DEMUX_H
#define MP4_DEMUX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint32_t first_chunk;
    uint32_t samples_per_chunk;
    uint32_t sample_desc_idx;
} Mp4StscEntry;

typedef struct {
    uint32_t sample_count;
    uint32_t sample_delta;
} Mp4SttsEntry;

typedef struct Mp4Demuxer {
    FILE *fp;

    // Track audio parameters
    uint32_t timescale;
    uint64_t track_duration;
    uint32_t sample_rate;
    uint16_t num_channels;
    uint16_t bits_per_sample;
    uint64_t total_pcm_frames;

    // Decoder Specific Info
    uint8_t *dsi;
    uint32_t dsi_len;

    // Sample tables
    uint32_t sample_count;
    uint32_t *sample_sizes;     // From stsz
    uint32_t const_sample_size; // If stsz has fixed size

    uint32_t chunk_count;
    uint64_t *chunk_offsets;    // From stco or co64

    uint32_t stsc_count;
    Mp4StscEntry *stsc_table;   // From stsc

    uint32_t stts_count;
    Mp4SttsEntry *stts_table;   // From stts

    uint32_t audio_track_id;

    // Cached sample to chunk map
    uint64_t *sample_byte_offsets;
    uint32_t *sample_durations; // Per-sample durations for fMP4 / DASH
} Mp4Demuxer;

Mp4Demuxer* mp4_demux_open(FILE *fp);
void        mp4_demux_close(Mp4Demuxer *d);

// Returns file offset and packet byte size for sample_idx [0 .. sample_count - 1]
bool        mp4_demux_get_sample_pos(const Mp4Demuxer *d, uint32_t sample_idx, uint64_t *out_offset, uint32_t *out_size);

// Maps a target PCM frame to the nearest AAC sample index
uint32_t    mp4_demux_frame_to_sample(const Mp4Demuxer *d, uint64_t target_frame);

#endif // MP4_DEMUX_H