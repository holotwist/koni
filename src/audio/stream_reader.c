#define _DEFAULT_SOURCE
#include "stream_reader.h"
#include "state.h"
#include "extension.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "listening_profile.h"

void stream_reader_close(AudioStream *stream) {
    if (!stream || !stream->is_open) return;

    // Guarantee skip and listen duration recording before stream destruction
    if (!stream->profile_logged) {
        uint32_t played_sec = atomic_load(&p_current_sec);
        uint32_t total_sec = atomic_load(&p_total_sec);
        float rms = stream->energy_samples > 0 ? sqrtf(stream->energy_accum / (float)stream->energy_samples) : 0.15f;
        float norm_energy = fminf(1.0f, rms * 3.5f);

        listening_profile_on_track_end(stream->filepath, played_sec, total_sec, norm_energy);
        stream->profile_logged = true;
    }

    if (active_decoder == stream->dec) {
        pthread_mutex_lock(&state_mutex);
        if (active_decoder == stream->dec) {
            active_codec = NULL;
            active_decoder = NULL;
        }
        pthread_mutex_unlock(&state_mutex);
    }

    if (stream->codec && stream->dec) {
        stream->codec->close(stream->dec);
    }
    koni_metadata_free(&stream->meta);
    koni_extensions_on_track_stopped();
    memset(stream, 0, sizeof(AudioStream));
}

bool stream_reader_open(AudioStream *stream, const char *path, const char *name, int idx) {
    if (!path || !path[0]) return false;
    stream_reader_close(stream);

    const KoniCodecImpl *codec = koni_find_codec_by_ext(path);
    if (!codec) return false;

    KoniDecoder *dec = codec->open(path);
    if (!dec) return false;

    KoniAudioFormat fmt = {0};
    codec->get_format(dec, &fmt);

    KoniMetadata meta = {0};
    uint32_t dur = 0;
    if (codec->read_metadata) {
        codec->read_metadata(path, &meta, &dur);
    }

    if (fmt.total_samples == 0 && dur > 0 && fmt.sample_rate > 0) {
        fmt.total_samples = (uint64_t)dur * fmt.sample_rate;
    }

    strncpy(stream->filepath, path, sizeof(stream->filepath) - 1);
    if (name) strncpy(stream->filename, name, sizeof(stream->filename) - 1);
    stream->file_idx = idx;
    stream->codec = codec;
    stream->dec = dec;
    stream->fmt = fmt;
    stream->meta = meta;
    stream->frames_decoded = 0;
    stream->reached_eof = false;
    stream->profile_logged = false;
    stream->energy_accum = 0.0f;
    stream->energy_samples = 0;
    rgain_init(&stream->rgain_state, fmt.sample_rate, fmt.num_channels);
    rgain_set_meta(&stream->rgain_state, meta.has_track_gain, meta.track_gain);
    stream->is_open = true;
    return true;
}

void stream_reader_apply_to_global_state(const AudioStream *stream) {
    pthread_mutex_lock(&state_mutex);
    koni_metadata_free(&p_metadata);
    p_format = stream->fmt;

    if (stream->meta.title) p_metadata.title = strdup(stream->meta.title);
    if (stream->meta.artist) p_metadata.artist = strdup(stream->meta.artist);
    if (stream->meta.album) p_metadata.album = strdup(stream->meta.album);
    if (stream->meta.lyrics) p_metadata.lyrics = strdup(stream->meta.lyrics);
    if (stream->meta.art_url) p_metadata.art_url = strdup(stream->meta.art_url);
    p_metadata.has_track_gain = stream->meta.has_track_gain;
    p_metadata.track_gain = stream->meta.track_gain;

    strncpy(playing_filepath, stream->filepath, sizeof(playing_filepath) - 1);
    strncpy(playing_filename, stream->filename, sizeof(playing_filename) - 1);
    playing_file_idx = stream->file_idx;
    active_codec = stream->codec;
    active_decoder = stream->dec;
    pthread_mutex_unlock(&state_mutex);

    koni_extensions_on_track_loaded(stream->filepath, stream->codec, stream->dec);

    atomic_store(&p_total_sec, (stream->fmt.sample_rate > 0) ? (stream->fmt.total_samples / stream->fmt.sample_rate) : 0);
    atomic_store(&vis_srate, stream->fmt.sample_rate);
    atomic_store(&p_current_sec, 0);
    atomic_store(&header_ready_for_idx, stream->file_idx);
    atomic_fetch_add(&current_track_id, 1);
    force_redraw = true;
}

uint32_t stream_reader_decode(AudioStream *stream, int32_t *pcm_out, uint32_t max_frames) {
    if (!stream || !stream->is_open || stream->reached_eof) return 0;
    uint32_t read = stream->codec->decode(stream->dec, pcm_out, max_frames);
    stream->frames_decoded += read;
    if (read < max_frames) {
        stream->reached_eof = true;
    }
    return read;
}

bool stream_reader_seek(AudioStream *stream, uint64_t target_sample) {
    if (!stream || !stream->is_open || !stream->codec || !stream->dec) return false;
    if (stream->codec->seek(stream->dec, target_sample)) {
        stream->frames_decoded = target_sample;
        stream->reached_eof = false;
        return true;
    }
    return false;
}

bool stream_reader_format_matches(const AudioStream *a, const AudioStream *b) {
    if (!a || !b || !a->is_open || !b->is_open) return false;
    return (a->fmt.sample_rate == b->fmt.sample_rate && a->fmt.num_channels == b->fmt.num_channels);
}

uint64_t stream_reader_remaining_samples(const AudioStream *stream) {
    if (!stream || !stream->is_open) return 0;
    return (stream->fmt.total_samples > stream->frames_decoded) ?
           (stream->fmt.total_samples - stream->frames_decoded) : 0;
}