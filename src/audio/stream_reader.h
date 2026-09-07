#ifndef STREAM_READER_H
#define STREAM_READER_H

#include "codec.h"
#include "replaygain/replaygain.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char filepath[1024];
    char filename[256];
    int file_idx;
    const KoniCodecImpl *codec;
    KoniDecoder *dec;
    KoniAudioFormat fmt;
    KoniMetadata meta;
    RGainState rgain_state;
    uint64_t frames_decoded;
    bool reached_eof;
    bool is_open;
} AudioStream;

bool stream_reader_open(AudioStream *stream, const char *path, const char *name, int idx);
void stream_reader_close(AudioStream *stream);
void stream_reader_apply_to_global_state(const AudioStream *stream);
uint32_t stream_reader_decode(AudioStream *stream, int32_t *pcm_out, uint32_t max_frames);
bool stream_reader_seek(AudioStream *stream, uint64_t target_sample);
bool stream_reader_format_matches(const AudioStream *a, const AudioStream *b);
uint64_t stream_reader_remaining_samples(const AudioStream *stream);

#endif // STREAM_READER_H