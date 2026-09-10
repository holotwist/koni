#define _DEFAULT_SOURCE
#include "audio.h"
#include "output_device.h"
#include "stream_reader.h"
#include "dsp_rack.h"
#include "krystal_engine.h"
#include "state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define CHUNK_FRAMES 16384

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

void *audio_thread_func(void *arg) {
    (void)arg;

    // Flush denormals to zero at hardware level for audio thread
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#elif defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1 << 24); // Set FZ (Flush-to-zero) bit
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#endif

    struct timespec sleep_ts = {0, 20000000L}; // 20ms

    dsp_rack_init();

    AudioStream cur_stream = {0};
    AudioStream next_stream = {0};
    bool gapless_active = false;
    KoniAudioFormat current_device_fmt = {0};

    int32_t *interleaved = malloc(sizeof(int32_t) * CHUNK_FRAMES * 8);
    float *float_output = malloc(sizeof(float) * CHUNK_FRAMES * 8);

    while (1) {
        PlayerCommand cmd = atomic_load(&current_cmd_atomic);

        if (cmd == CMD_QUIT) {
            stream_reader_close(&cur_stream);
            stream_reader_close(&next_stream);
            break;
        }

        if (cmd == CMD_NEXT || cmd == CMD_PREV) {
            stream_reader_close(&next_stream);
            output_device_clear_gapless();
            gapless_active = false;
            atomic_store(&seek_target_ms, -1);

            if (player_advance_track(cmd)) {
                cmd = CMD_PLAY;
                atomic_store(&current_cmd_atomic, CMD_PLAY);
            } else {
                stream_reader_close(&cur_stream);
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

            stream_reader_close(&next_stream);
            output_device_clear_gapless();
            gapless_active = false;

            if (!stream_reader_open(&cur_stream, path, name, idx)) {
                atomic_store(&play_state_atomic, STATE_STOPPED);
                atomic_store(&current_cmd_atomic, CMD_NONE);
                continue;
            }

            output_device_stop();
            output_device_reset_buffer();

            atomic_store(&p_frames_consumed, 0);
            stream_reader_apply_to_global_state(&cur_stream);

            // Seek to restored position if available
            int saved_target_ms = atomic_load(&seek_target_ms);
            if (saved_target_ms > 0) {
                uint64_t target_sample = ((uint64_t)saved_target_ms * cur_stream.fmt.sample_rate) / 1000ULL;
                if (stream_reader_seek(&cur_stream, target_sample)) {
                    atomic_store(&p_frames_consumed, (uint32_t)target_sample);
                    atomic_store(&p_current_sec, (uint32_t)(target_sample / cur_stream.fmt.sample_rate));
                }
                atomic_store(&seek_target_ms, -1);
            }

            atomic_store(&current_cmd_atomic, CMD_NONE);
            atomic_store(&play_state_atomic, STATE_PLAYING);

            dsp_rack_reset();
            atomic_store(&vis_wpos, 0);
            memset(vis_ring_l, 0, sizeof(vis_ring_l));
            memset(vis_ring_r, 0, sizeof(vis_ring_r));
        }

        PlayState state = atomic_load(&play_state_atomic);
        if (state != STATE_PLAYING && state != STATE_PAUSED) {
            output_device_uninit();
            stream_reader_close(&cur_stream);
            stream_reader_close(&next_stream);
            output_device_clear_gapless();
            gapless_active = false;
            nanosleep(&sleep_ts, NULL);
            continue;
        }

        if (!cur_stream.is_open) {
            atomic_store(&play_state_atomic, STATE_STOPPED);
            continue;
        }

        // Initialize or re-init hardware output device if sample format changed
        if (!output_device_is_active() ||
            current_device_fmt.sample_rate != cur_stream.fmt.sample_rate ||
            current_device_fmt.num_channels != cur_stream.fmt.num_channels) {

            if (!output_device_init(cur_stream.fmt.sample_rate, cur_stream.fmt.num_channels)) {
                stream_reader_close(&cur_stream);
                atomic_store(&play_state_atomic, STATE_STOPPED);
                continue;
            }
            current_device_fmt = cur_stream.fmt;
        }

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
                atomic_store(&seek_target_ms, -1);
                atomic_store(&current_cmd_atomic, CMD_NONE);

                if (target_ms >= 0) {
                    uint64_t target_sample = ((uint64_t)target_ms * cur_stream.fmt.sample_rate) / 1000ULL;
                    if (stream_reader_seek(&cur_stream, target_sample)) {
                        stream_reader_close(&next_stream);
                        output_device_clear_gapless();
                        gapless_active = false;

                        output_device_stop();
                        output_device_reset_buffer();

                        atomic_store(&p_frames_consumed, (uint32_t)target_sample);
                        atomic_store(&p_current_sec, (uint32_t)(target_sample / cur_stream.fmt.sample_rate));

                        dsp_rack_reset();
                        memset(vis_ring_l, 0, sizeof(vis_ring_l));
                        memset(vis_ring_r, 0, sizeof(vis_ring_r));
                        output_device_start();
                    }
                }
                continue;
            }

            if (atomic_load(&play_state_atomic) == STATE_PAUSED) {
                output_device_stop();
                nanosleep(&sleep_ts, NULL);
                continue;
            } else {
                output_device_start();
            }

            // Check if audio hardware reached gapless boundary
            if (gapless_active && output_device_check_gapless_switched()) {
                gapless_active = false;
                player_advance_track(CMD_NEXT_AUTO);
                stream_reader_close(&cur_stream);
                cur_stream = next_stream;
                memset(&next_stream, 0, sizeof(AudioStream));
                stream_reader_apply_to_global_state(&cur_stream);
            }

            // Preload next track within 3 seconds of track completion
            uint64_t remaining = stream_reader_remaining_samples(&cur_stream);
            if (!next_stream.is_open && !cur_stream.reached_eof && remaining > 0 &&
                remaining <= (uint64_t)cur_stream.fmt.sample_rate * 3ULL) {
                char np[1024] = {0}, nn[256] = {0};
                int ni = -1;
                if (player_peek_next_track(np, sizeof(np), nn, sizeof(nn), &ni)) {
                    stream_reader_open(&next_stream, np, nn, ni);
                }
            }

            uint32_t chunk_request = CHUNK_FRAMES;
            uint32_t mix_samples = 0;

            if (!cur_stream.reached_eof) {
                mix_samples = stream_reader_decode(&cur_stream, interleaved, chunk_request);
            }

            // Seamless gapless concatenation if formats match
            if (cur_stream.reached_eof) {
                if (!next_stream.is_open) {
                    char np[1024] = {0}, nn[256] = {0};
                    int ni = -1;
                    if (player_peek_next_track(np, sizeof(np), nn, sizeof(nn), &ni)) {
                        stream_reader_open(&next_stream, np, nn, ni);
                    }
                }

                if (stream_reader_format_matches(&cur_stream, &next_stream)) {
                    if (!gapless_active) {
                        uint32_t in_rb = output_device_available_read() + mix_samples;
                        output_device_arm_gapless(in_rb);
                        gapless_active = true;
                    }

                    uint32_t needed = chunk_request - mix_samples;
                    if (needed > 0 && !next_stream.reached_eof) {
                        int32_t *gapless_buf = interleaved + (mix_samples * cur_stream.fmt.num_channels);
                        uint32_t next_mix = stream_reader_decode(&next_stream, gapless_buf, needed);
                        mix_samples += next_mix;
                    }
                }
            }

            // Exit when all decoded samples finish playing through hardware
            if (mix_samples == 0) {
                if (cur_stream.reached_eof && !gapless_active) {
                    if (output_device_available_read() > 0) {
                        nanosleep(&sleep_ts, NULL);
                        continue;
                    }
                }
                break;
            }

            // Run float DSP chain (ReplayGain, EQ, Soft Limiter, TPDF Dither, Vis Tap)
            dsp_rack_process(interleaved, float_output, mix_samples, cur_stream.fmt.num_channels,
                             cur_stream.fmt.sample_rate, &cur_stream.rgain_state,
                             atomic_load(&play_mode_rgain), atomic_load(&volume));

            // Feed into hardware output ring buffer
            uint32_t written = 0;
            while (written < mix_samples && !exit_track) {
                cmd = atomic_load(&current_cmd_atomic);
                if (cmd != CMD_NONE && cmd != CMD_SEEK) { exit_track = 1; break; }

                uint32_t frames_to_write = mix_samples - written;
                uint32_t w = output_device_write(float_output + (written * cur_stream.fmt.num_channels),
                                                 frames_to_write, cur_stream.fmt.num_channels);
                written += w;

                if (w == 0) {
                    struct timespec sleep_write = {0, 4000000L}; // 4ms backoff
                    nanosleep(&sleep_write, NULL);
                }
            }

            uint32_t consumed = atomic_load(&p_frames_consumed);
            atomic_store(&p_current_sec, (cur_stream.fmt.sample_rate > 0) ? (consumed / cur_stream.fmt.sample_rate) : 0);
        }

        // Transition fallback if gapless was not active
        cmd = atomic_load(&current_cmd_atomic);
        if (!exit_track && atomic_load(&play_state_atomic) == STATE_PLAYING && (cmd == CMD_NONE || cmd == CMD_NEXT_AUTO)) {
            if (gapless_active && next_stream.is_open) {
                output_device_clear_gapless();
                gapless_active = false;
                player_advance_track(CMD_NEXT_AUTO);
                stream_reader_close(&cur_stream);
                cur_stream = next_stream;
                memset(&next_stream, 0, sizeof(AudioStream));
                stream_reader_apply_to_global_state(&cur_stream);
            } else if (player_advance_track(CMD_NEXT_AUTO)) {
                stream_reader_close(&cur_stream);
                stream_reader_close(&next_stream);
                output_device_clear_gapless();
                gapless_active = false;
                atomic_store(&seek_target_ms, -1);

                char path[1024] = {0}, name[256] = {0};
                int idx = -1;

                pthread_mutex_lock(&state_mutex);
                strncpy(path, playing_filepath, sizeof(path) - 1);
                strncpy(name, playing_filename, sizeof(name) - 1);
                idx = playing_file_idx;
                pthread_mutex_unlock(&state_mutex);

                if (stream_reader_open(&cur_stream, path, name, idx)) {
                    if (current_device_fmt.sample_rate == cur_stream.fmt.sample_rate &&
                        current_device_fmt.num_channels == cur_stream.fmt.num_channels) {
                        output_device_reset_buffer();
                    }
                    atomic_store(&p_frames_consumed, 0);
                    atomic_store(&p_current_sec, 0);
                    stream_reader_apply_to_global_state(&cur_stream);
                } else {
                    atomic_store(&play_state_atomic, STATE_STOPPED);
                }
            } else {
                stream_reader_close(&cur_stream);
                stream_reader_close(&next_stream);
                atomic_store(&play_state_atomic, STATE_STOPPED);
            }
        } else if (cmd == CMD_STOP) {
            stream_reader_close(&cur_stream);
            stream_reader_close(&next_stream);
            output_device_clear_gapless();
            gapless_active = false;
            atomic_store(&play_state_atomic, STATE_STOPPED);
            atomic_store(&current_cmd_atomic, CMD_NONE);
        }
    }

    free(interleaved);
    free(float_output);
    krystal_shutdown();
    output_device_uninit();
    return NULL;
}