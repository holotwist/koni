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

void *audio_thread_func(void *arg) {
    (void)arg;
    struct timespec sleep_ts = {0, 50000000L}; // 50ms

    while (1) {
        PlayerCommand cmd = atomic_load(&current_cmd_atomic);
        char filepath[1024];
        int this_file_idx = -1;

        if (cmd == CMD_PLAY) {
            atomic_store(&header_ready_for_idx, -1);
            pthread_mutex_lock(&state_mutex);
            strncpy(filepath, playing_filepath, sizeof(filepath));
            this_file_idx = playing_file_idx;
            pthread_mutex_unlock(&state_mutex);
            
            atomic_store(&current_cmd_atomic, CMD_NONE);
            atomic_store(&play_state_atomic, STATE_PLAYING);
            
            // Reset metrics
            atomic_store(&p_current_sec, 0); atomic_store(&p_total_sec, 0);
            memset(vis_ring_l, 0, sizeof(vis_ring_l)); memset(vis_ring_r, 0, sizeof(vis_ring_r));
            atomic_store(&vis_wpos, 0); atomic_store(&p_frames_consumed, 0);
        } else if (cmd == CMD_QUIT) {
            break;
        }

        PlayState state = atomic_load(&play_state_atomic);
        if (state != STATE_PLAYING && state != STATE_PAUSED) {
            nanosleep(&sleep_ts, NULL);
            continue;
        }

        const KoniCodecImpl* codec = koni_find_codec_by_ext(filepath);
        if (!codec) { atomic_store(&play_state_atomic, STATE_STOPPED); continue; }

        KoniDecoder* dec = codec->open(filepath);
        if (!dec) { atomic_store(&play_state_atomic, STATE_STOPPED); continue; }

        KoniAudioFormat fmt; KoniMetadata meta;
        codec->get_format(dec, &fmt);
        codec->get_metadata(dec, &meta);

        // Save generic metadata to state
        pthread_mutex_lock(&state_mutex);
        koni_metadata_free(&p_metadata);
        p_format = fmt;
        p_metadata = meta; 
        pthread_mutex_unlock(&state_mutex);
        
        atomic_store(&p_total_sec, (fmt.sample_rate > 0) ? (fmt.total_samples / fmt.sample_rate) : 0);
        atomic_store(&vis_srate, fmt.sample_rate);
        atomic_store(&header_ready_for_idx, this_file_idx);

        ma_pcm_rb ring_buffer;
        ma_pcm_rb_init(ma_format_s32, fmt.num_channels, fmt.sample_rate / 2, NULL, NULL, &ring_buffer);
        
        ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
        deviceConfig.playback.format   = ma_format_s32;
        deviceConfig.playback.channels = fmt.num_channels;
        deviceConfig.sampleRate        = fmt.sample_rate;
        deviceConfig.dataCallback      = data_callback;
        deviceConfig.pUserData         = &ring_buffer;
        
        ma_device device;
        if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
            ma_pcm_rb_uninit(&ring_buffer); codec->close(dec); atomic_store(&play_state_atomic, STATE_STOPPED); continue;
        }
        ma_device_start(&device);
        bool device_active = true;

        int32_t *interleaved = malloc(sizeof(int32_t) * 65536 * fmt.num_channels);
        uint32_t samples_played = 0;
        int exit_loop = 0;
        
        RGainState rgain_state;
        rgain_init(&rgain_state, fmt.sample_rate, fmt.num_channels);
        rgain_set_meta(&rgain_state, meta.has_track_gain, meta.track_gain);

        while (samples_played < fmt.total_samples && !exit_loop) {
            cmd = atomic_load(&current_cmd_atomic);
            if (cmd == CMD_STOP || cmd == CMD_NEXT || cmd == CMD_PREV || cmd == CMD_PLAY || cmd == CMD_QUIT) {
                exit_loop = 1; break;
            }
            if (cmd == CMD_PAUSE) {
                atomic_store(&play_state_atomic, (atomic_load(&play_state_atomic) == STATE_PLAYING) ? STATE_PAUSED : STATE_PLAYING);
                atomic_store(&current_cmd_atomic, CMD_NONE);
            }
            if (cmd == CMD_SEEK) {
                int target_sec = atomic_load(&seek_target_sec);
                atomic_store(&current_cmd_atomic, CMD_NONE);
                if (target_sec >= 0) {
                    uint64_t target_sample = (uint64_t)target_sec * fmt.sample_rate;
                    if (codec->seek(dec, target_sample)) {
                        samples_played = target_sample;
                        atomic_store(&p_current_sec, samples_played / fmt.sample_rate);
                        atomic_store(&p_frames_consumed, target_sample);
                        
                        ma_device_stop(&device);
                        ma_pcm_rb_reset(&ring_buffer);
                        
                        memset(vis_ring_l, 0, sizeof(vis_ring_l)); 
                        memset(vis_ring_r, 0, sizeof(vis_ring_r));
                        
                        ma_device_start(&device); device_active = true;
                    }
                }
                continue;
            }

            if (atomic_load(&play_state_atomic) == STATE_PAUSED) {
                if (device_active) { ma_device_stop(&device); device_active = false; }
                nanosleep(&sleep_ts, NULL); continue;
            } else if (!device_active) { ma_device_start(&device); device_active = true; }
            
            uint32_t mix_samples = codec->decode(dec, interleaved, 16384);
            if (mix_samples == 0) {
                 break; // EOF
            }
            
            // Apply ReplayGain dynamically before volume
            rgain_set_mode(&rgain_state, (RGainMode)atomic_load(&play_mode_rgain));
            rgain_process(&rgain_state, interleaved, mix_samples);
            
            // Handle Master volume application
            int current_vol = atomic_load(&volume);
            for (uint32_t s = 0; s < mix_samples * fmt.num_channels; s++) {
                long long val64 = ((long long)interleaved[s] * current_vol) / 100;
                if (val64 > 2147483647LL) val64 = 2147483647LL;
                else if (val64 < -2147483648LL) val64 = -2147483648LL;
                interleaved[s] = (int32_t)val64;
            }
            
            // Update visualizer ring buffer
            uint32_t local_wpos = atomic_load(&vis_wpos);
            for (uint32_t s = 0; s < mix_samples; s++) {
                float vl = 0.0f, vr = 0.0f;
                if (fmt.num_channels == 1) {
                    vl = vr = (float)(interleaved[s]) / 2147483648.0f;
                } else if (fmt.num_channels >= 2) {
                    vl = (float)(interleaved[s * fmt.num_channels]) / 2147483648.0f;
                    vr = (float)(interleaved[s * fmt.num_channels + 1]) / 2147483648.0f;
                }
                vis_ring_l[local_wpos & VIS_BUF_MASK] = vl;
                vis_ring_r[local_wpos & VIS_BUF_MASK] = vr;
                local_wpos++;
            }
            atomic_store(&vis_wpos, local_wpos);

            // Feed to miniaudio
            uint32_t written = 0;
            while (written < mix_samples && !exit_loop) {
                ma_uint32 framesToWrite = mix_samples - written;
                void* pWriteBuffer;
                ma_pcm_rb_acquire_write(&ring_buffer, &framesToWrite, &pWriteBuffer);
                
                if (framesToWrite > 0) {
                    memcpy(pWriteBuffer, interleaved + (written * fmt.num_channels), 
                           framesToWrite * sizeof(int32_t) * fmt.num_channels);
                    ma_pcm_rb_commit_write(&ring_buffer, framesToWrite);
                    written += framesToWrite;
                } else {
                    struct timespec sleep_ts_write = {0, 5000000L}; // 5ms sleep if full
                    nanosleep(&sleep_ts_write, NULL);
                }
            }

            samples_played += mix_samples;
            atomic_store(&p_current_sec, samples_played / fmt.sample_rate);
        }
        
        ma_device_uninit(&device);
        ma_pcm_rb_uninit(&ring_buffer);
        codec->close(dec);
        free(interleaved);

        cmd = atomic_load(&current_cmd_atomic);
        if (atomic_load(&play_state_atomic) == STATE_PLAYING && cmd == CMD_NONE) {
            atomic_store(&play_state_atomic, STATE_STOPPED);
            atomic_store(&current_cmd_atomic, CMD_NEXT_AUTO);
        } else if (cmd == CMD_STOP) {
            atomic_store(&play_state_atomic, STATE_STOPPED);
            atomic_store(&current_cmd_atomic, CMD_NONE);
        }
    }
    return NULL;
}