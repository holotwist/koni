#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "audio.h"
#include "state.h"
#include "codec.h"
#include "replaygain/replaygain.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    ma_pcm_rb* pRingBuffer = (ma_pcm_rb*)pDevice->pUserData;
    ma_uint32 framesReadTotal = 0;
    ma_uint32 bpf = ma_get_bytes_per_frame(pDevice->playback.format, pDevice->playback.channels);
    uint8_t* pOut = (uint8_t*)pOutput;

    while (framesReadTotal < frameCount) {
        ma_uint32 framesToRead = frameCount - framesReadTotal;
        void* pReadBuffer;
        ma_pcm_rb_acquire_read(pRingBuffer, &framesToRead, &pReadBuffer);
        if (framesToRead == 0) break;
        
        memcpy(pOut, pReadBuffer, framesToRead * bpf);
        ma_pcm_rb_commit_read(pRingBuffer, framesToRead);
        
        pOut += framesToRead * bpf;
        framesReadTotal += framesToRead;
    }
    
    if (framesReadTotal < frameCount) {
        memset(pOut, 0, (frameCount - framesReadTotal) * bpf);
    }
    atomic_fetch_add(&p_frames_consumed, framesReadTotal);
}

typedef struct {
    char filepath[1024];
    char filename[256];
    int file_idx;
    const KoniCodecImpl *codec;
    KoniDecoder *dec;
    KoniAudioFormat fmt;
    KoniMetadata meta;
    RGainState rgain_state;
    uint64_t samples_played;
    bool is_open;
} AudioStream;

static void audio_stream_close(AudioStream *stream) {
    if (!stream || !stream->is_open) return;
    if (stream->codec && stream->dec) {
        stream->codec->close(stream->dec);
    }
    koni_metadata_free(&stream->meta);
    memset(stream, 0, sizeof(AudioStream));
}

static bool audio_stream_open(AudioStream *stream, const char *path, const char *name, int idx) {
    if (!path || !path[0]) return false;
    audio_stream_close(stream);

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

    strncpy(stream->filepath, path, sizeof(stream->filepath) - 1);
    if (name) strncpy(stream->filename, name, sizeof(stream->filename) - 1);
    stream->file_idx = idx;
    stream->codec = codec;
    stream->dec = dec;
    stream->fmt = fmt;
    stream->meta = meta;
    stream->samples_played = 0;
    rgain_init(&stream->rgain_state, fmt.sample_rate, fmt.num_channels);
    rgain_set_meta(&stream->rgain_state, meta.has_track_gain, meta.track_gain);
    stream->is_open = true;
    return true;
}

static void apply_stream_to_global_state(const AudioStream *stream) {
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
    pthread_mutex_unlock(&state_mutex);

    atomic_store(&p_total_sec, (stream->fmt.sample_rate > 0) ? (stream->fmt.total_samples / stream->fmt.sample_rate) : 0);
    atomic_store(&vis_srate, stream->fmt.sample_rate);
    atomic_store(&p_current_sec, 0);
    atomic_store(&header_ready_for_idx, stream->file_idx);
    atomic_fetch_add(&current_track_id, 1);
}

void *audio_thread_func(void *arg) {
    (void)arg;
    struct timespec sleep_ts = {0, 20000000L}; // 20ms

    ma_device device;
    ma_pcm_rb ring_buffer;
    bool device_initialized = false;
    KoniAudioFormat current_device_fmt = {0};

    AudioStream cur_stream = {0};
    AudioStream next_stream = {0};

    while (1) {
        PlayerCommand cmd = atomic_load(&current_cmd_atomic);

        if (cmd == CMD_QUIT) {
            audio_stream_close(&cur_stream);
            audio_stream_close(&next_stream);
            break;
        }

        if (cmd == CMD_NEXT || cmd == CMD_PREV) {
            audio_stream_close(&next_stream);
            if (player_advance_track(cmd)) {
                cmd = CMD_PLAY;
                atomic_store(&current_cmd_atomic, CMD_PLAY);
            } else {
                audio_stream_close(&cur_stream);
                atomic_store(&current_cmd_atomic, CMD_STOP);
                atomic_store(&play_state_atomic, STATE_STOPPED);
                continue;
            }
        }

        if (cmd == CMD_PLAY) {
            atomic_store(&header_ready_for_idx, -1);
            char path[1024] = {0};
            char name[256] = {0};
            int idx = -1;

            pthread_mutex_lock(&state_mutex);
            strncpy(path, playing_filepath, sizeof(path) - 1);
            strncpy(name, playing_filename, sizeof(name) - 1);
            idx = playing_file_idx;
            pthread_mutex_unlock(&state_mutex);

            audio_stream_close(&next_stream);
            if (!audio_stream_open(&cur_stream, path, name, idx)) {
                atomic_store(&play_state_atomic, STATE_STOPPED);
                atomic_store(&current_cmd_atomic, CMD_NONE);
                continue;
            }

            apply_stream_to_global_state(&cur_stream);
            atomic_store(&current_cmd_atomic, CMD_NONE);
            atomic_store(&play_state_atomic, STATE_PLAYING);

            atomic_store(&vis_wpos, 0);
            atomic_store(&p_frames_consumed, 0);
            memset(vis_ring_l, 0, sizeof(vis_ring_l));
            memset(vis_ring_r, 0, sizeof(vis_ring_r));

            if (device_initialized) {
                ma_device_stop(&device);
                ma_pcm_rb_reset(&ring_buffer);
            }
        }

        PlayState state = atomic_load(&play_state_atomic);
        if (state != STATE_PLAYING && state != STATE_PAUSED) {
            if (device_initialized) {
                ma_device_uninit(&device);
                ma_pcm_rb_uninit(&ring_buffer);
                device_initialized = false;
            }
            audio_stream_close(&cur_stream);
            audio_stream_close(&next_stream);
            nanosleep(&sleep_ts, NULL);
            continue;
        }

        if (!cur_stream.is_open) {
            atomic_store(&play_state_atomic, STATE_STOPPED);
            continue;
        }

        // Initialize or re-init audio output device if sample format changed
        if (!device_initialized || 
            current_device_fmt.sample_rate != cur_stream.fmt.sample_rate || 
            current_device_fmt.num_channels != cur_stream.fmt.num_channels) {
            
            if (device_initialized) {
                ma_uint32 available = ma_pcm_rb_available_read(&ring_buffer);
                while (available > 0 && atomic_load(&current_cmd_atomic) == CMD_NONE) {
                    nanosleep(&sleep_ts, NULL);
                    available = ma_pcm_rb_available_read(&ring_buffer);
                }
                ma_device_uninit(&device);
                ma_pcm_rb_uninit(&ring_buffer);
            }

            ma_pcm_rb_init(ma_format_s32, cur_stream.fmt.num_channels, cur_stream.fmt.sample_rate / 2, NULL, NULL, &ring_buffer);
            
            ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
            deviceConfig.playback.format   = ma_format_s32;
            deviceConfig.playback.channels = cur_stream.fmt.num_channels;
            deviceConfig.sampleRate        = cur_stream.fmt.sample_rate;
            deviceConfig.dataCallback      = data_callback;
            deviceConfig.pUserData         = &ring_buffer;
            
            if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
                ma_pcm_rb_uninit(&ring_buffer);
                audio_stream_close(&cur_stream);
                atomic_store(&play_state_atomic, STATE_STOPPED);
                continue;
            }
            ma_device_start(&device);
            device_initialized = true;
            current_device_fmt = cur_stream.fmt;
        }

        bool device_active = (ma_device_get_state(&device) == ma_device_state_started);
        int32_t *interleaved = malloc(sizeof(int32_t) * 16384 * cur_stream.fmt.num_channels);
        int exit_track = 0;

        while (!exit_track) {
            cmd = atomic_load(&current_cmd_atomic);
            if (cmd == CMD_STOP || cmd == CMD_NEXT || cmd == CMD_PREV || cmd == CMD_PLAY || cmd == CMD_QUIT) {
                exit_track = 1;
                break;
            }

            if (cmd == CMD_PAUSE) {
                atomic_store(&play_state_atomic, (atomic_load(&play_state_atomic) == STATE_PLAYING) ? STATE_PAUSED : STATE_PLAYING);
                atomic_store(&current_cmd_atomic, CMD_NONE);
            }

            if (cmd == CMD_SEEK) {
                int target_ms = atomic_load(&seek_target_ms);
                atomic_store(&current_cmd_atomic, CMD_NONE);
                if (target_ms >= 0 && cur_stream.codec && cur_stream.dec) {
                    uint64_t target_sample = ((uint64_t)target_ms * cur_stream.fmt.sample_rate) / 1000ULL;
                    if (cur_stream.codec->seek(cur_stream.dec, target_sample)) {
                        cur_stream.samples_played = target_sample;
                        atomic_store(&p_current_sec, cur_stream.samples_played / cur_stream.fmt.sample_rate);
                        atomic_store(&p_frames_consumed, target_sample);
                        
                        ma_device_stop(&device);
                        ma_pcm_rb_reset(&ring_buffer);
                        memset(vis_ring_l, 0, sizeof(vis_ring_l));
                        memset(vis_ring_r, 0, sizeof(vis_ring_r));
                        ma_device_start(&device);
                        device_active = true;

                        // Close preloaded next stream if we seeked away
                        audio_stream_close(&next_stream);
                    }
                }
                continue;
            }

            if (atomic_load(&play_state_atomic) == STATE_PAUSED) {
                if (device_active) { ma_device_stop(&device); device_active = false; }
                nanosleep(&sleep_ts, NULL);
                continue;
            } else if (!device_active) {
                ma_device_start(&device);
                device_active = true;
            }

            // Preload next track if within 3 seconds of track completion
            uint64_t remaining_samples = (cur_stream.fmt.total_samples > cur_stream.samples_played) ? 
                                         (cur_stream.fmt.total_samples - cur_stream.samples_played) : 0;
            if (!next_stream.is_open && remaining_samples > 0 && remaining_samples <= (uint64_t)cur_stream.fmt.sample_rate * 3ULL) {
                char next_p[1024] = {0};
                char next_n[256] = {0};
                int next_i = -1;
                if (player_peek_next_track(next_p, sizeof(next_p), next_n, sizeof(next_n), &next_i)) {
                    audio_stream_open(&next_stream, next_p, next_n, next_i);
                }
            }

            uint32_t chunk_request = 16384;
            uint32_t mix_samples = cur_stream.codec->decode(cur_stream.dec, interleaved, chunk_request);

            if (mix_samples > 0) {
                rgain_set_mode(&cur_stream.rgain_state, (RGainMode)atomic_load(&play_mode_rgain));
                rgain_process(&cur_stream.rgain_state, interleaved, mix_samples);
            }

            // Seamlessly fill the rest of the block from preloaded track
            if (mix_samples < chunk_request && next_stream.is_open && 
                next_stream.fmt.sample_rate == cur_stream.fmt.sample_rate && 
                next_stream.fmt.num_channels == cur_stream.fmt.num_channels) {

                uint32_t needed = chunk_request - mix_samples;
                int32_t *gapless_buf = interleaved + (mix_samples * cur_stream.fmt.num_channels);
                uint32_t next_mix = next_stream.codec->decode(next_stream.dec, gapless_buf, needed);

                if (next_mix > 0) {
                    rgain_set_mode(&next_stream.rgain_state, (RGainMode)atomic_load(&play_mode_rgain));
                    rgain_process(&next_stream.rgain_state, gapless_buf, next_mix);
                    next_stream.samples_played += next_mix;
                }

                // Advance player state without interrupting audio playback
                player_advance_track(CMD_NEXT_AUTO);
                audio_stream_close(&cur_stream);
                cur_stream = next_stream;
                memset(&next_stream, 0, sizeof(AudioStream));
                apply_stream_to_global_state(&cur_stream);
                mix_samples += next_mix;
            }

            if (mix_samples == 0) {
                break; // Track reached end
            }

            // Apply cubic volume scaling
            int current_vol = atomic_load(&volume);
            float vol_factor = 0.0f;
            if (current_vol <= 0) {
                vol_factor = 0.0f;
            } else if (current_vol <= 100) {
                float norm = (float)current_vol / 100.0f;
                vol_factor = norm * norm * norm;
            } else {
                float boost = (float)(current_vol - 100) / 100.0f;
                vol_factor = 1.0f + boost;
            }

            if (vol_factor != 1.0f) {
                for (uint32_t s = 0; s < mix_samples * cur_stream.fmt.num_channels; s++) {
                    long long val64 = (long long)((float)interleaved[s] * vol_factor);
                    if (val64 > 2147483647LL) val64 = 2147483647LL;
                    else if (val64 < -2147483648LL) val64 = -2147483648LL;
                    interleaved[s] = (int32_t)val64;
                }
            }

            // Feed visualizer ring buffer
            uint32_t local_wpos = atomic_load(&vis_wpos);
            for (uint32_t s = 0; s < mix_samples; s++) {
                float vl = 0.0f, vr = 0.0f;
                if (cur_stream.fmt.num_channels == 1) {
                    vl = vr = (float)(interleaved[s]) / 2147483648.0f;
                } else if (cur_stream.fmt.num_channels >= 2) {
                    vl = (float)(interleaved[s * cur_stream.fmt.num_channels]) / 2147483648.0f;
                    vr = (float)(interleaved[s * cur_stream.fmt.num_channels + 1]) / 2147483648.0f;
                }
                vis_ring_l[local_wpos & VIS_BUF_MASK] = vl;
                vis_ring_r[local_wpos & VIS_BUF_MASK] = vr;
                local_wpos++;
            }
            atomic_store(&vis_wpos, local_wpos);

            // Commit decoded PCM to miniaudio playback ring buffer
            uint32_t written = 0;
            while (written < mix_samples && !exit_track) {
                cmd = atomic_load(&current_cmd_atomic);
                if (cmd != CMD_NONE && cmd != CMD_SEEK) { exit_track = 1; break; }

                ma_uint32 framesToWrite = mix_samples - written;
                void *pWriteBuffer = NULL;
                ma_pcm_rb_acquire_write(&ring_buffer, &framesToWrite, &pWriteBuffer);
                
                if (framesToWrite > 0) {
                    memcpy(pWriteBuffer, interleaved + (written * cur_stream.fmt.num_channels), 
                           framesToWrite * sizeof(int32_t) * cur_stream.fmt.num_channels);
                    ma_pcm_rb_commit_write(&ring_buffer, framesToWrite);
                    written += framesToWrite;
                } else {
                    struct timespec sleep_ts_write = {0, 4000000L}; // 4ms backoff
                    nanosleep(&sleep_ts_write, NULL);
                }
            }

            cur_stream.samples_played += mix_samples;
            atomic_store(&p_current_sec, cur_stream.samples_played / cur_stream.fmt.sample_rate);
        }

        free(interleaved);

        // Fallback auto-advance if format mismatch prevented zero-gap merging
        cmd = atomic_load(&current_cmd_atomic);
        if (!exit_track && atomic_load(&play_state_atomic) == STATE_PLAYING && (cmd == CMD_NONE || cmd == CMD_NEXT_AUTO)) {
            if (next_stream.is_open) {
                player_advance_track(CMD_NEXT_AUTO);
                audio_stream_close(&cur_stream);
                cur_stream = next_stream;
                memset(&next_stream, 0, sizeof(AudioStream));
                apply_stream_to_global_state(&cur_stream);
            } else if (player_advance_track(CMD_NEXT_AUTO)) {
                audio_stream_close(&cur_stream);
                char path[1024] = {0};
                char name[256] = {0};
                int idx = -1;

                pthread_mutex_lock(&state_mutex);
                strncpy(path, playing_filepath, sizeof(path) - 1);
                strncpy(name, playing_filename, sizeof(name) - 1);
                idx = playing_file_idx;
                pthread_mutex_unlock(&state_mutex);

                if (audio_stream_open(&cur_stream, path, name, idx)) {
                    apply_stream_to_global_state(&cur_stream);
                } else {
                    atomic_store(&play_state_atomic, STATE_STOPPED);
                }
            } else {
                audio_stream_close(&cur_stream);
                atomic_store(&play_state_atomic, STATE_STOPPED);
            }
        } else if (cmd == CMD_STOP) {
            audio_stream_close(&cur_stream);
            audio_stream_close(&next_stream);
            atomic_store(&play_state_atomic, STATE_STOPPED);
            atomic_store(&current_cmd_atomic, CMD_NONE);
        }
    }

    if (device_initialized) {
        ma_device_uninit(&device);
        ma_pcm_rb_uninit(&ring_buffer);
    }
    return NULL;
}