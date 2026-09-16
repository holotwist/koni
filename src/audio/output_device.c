#define _DEFAULT_SOURCE
#include "output_device.h"
#include "state.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <string.h>
#include <stdatomic.h>

static ma_device s_device;
static ma_pcm_rb s_ring_buffer;
static bool s_device_initialized = false;
static atomic_uint s_gapless_countdown = 0;
static atomic_bool s_gapless_switched = false;

static void data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount) {
    (void)pInput;
    ma_pcm_rb *pRingBuffer = (ma_pcm_rb*)pDevice->pUserData;
    ma_uint32 framesReadTotal = 0;
    ma_uint32 bpf = ma_get_bytes_per_frame(pDevice->playback.format, pDevice->playback.channels);
    uint8_t *pOut = (uint8_t*)pOutput;

    while (framesReadTotal < frameCount) {
        ma_uint32 framesToRead = frameCount - framesReadTotal;
        void *pReadBuffer = NULL;
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

    if (framesReadTotal > 0) {
        uint32_t cd = atomic_load(&s_gapless_countdown);
        if (cd == 0) {
            atomic_fetch_add(&p_frames_consumed, framesReadTotal);
        } else if (framesReadTotal < cd) {
            atomic_fetch_add(&p_frames_consumed, framesReadTotal);
            atomic_store(&s_gapless_countdown, cd - framesReadTotal);
        } else {
            // Track 1 finishes and Track 2 starts in this audio frame
            uint32_t t1_frames = cd;
            uint32_t t2_frames = framesReadTotal - cd;
            atomic_fetch_add(&p_frames_consumed, t1_frames);
            atomic_store(&s_gapless_countdown, 0);
            atomic_store(&p_frames_consumed, t2_frames);
            atomic_store(&s_gapless_switched, true);
        }
    }
}

bool output_device_init(uint32_t sample_rate, uint16_t channels) {
    output_device_uninit();

    if (sample_rate == 0) sample_rate = 44100;
    if (channels == 0) channels = 2;

    if (ma_pcm_rb_init(ma_format_f32, channels, sample_rate / 2, NULL, NULL, &s_ring_buffer) != MA_SUCCESS) {
        return false;
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = ma_format_f32;
    deviceConfig.playback.channels = channels;
    deviceConfig.sampleRate        = sample_rate;
    deviceConfig.dataCallback      = data_callback;
    deviceConfig.pUserData         = &s_ring_buffer;

    if (ma_device_init(NULL, &deviceConfig, &s_device) != MA_SUCCESS) {
        ma_pcm_rb_uninit(&s_ring_buffer);
        return false;
    }

    ma_device_start(&s_device);
    s_device_initialized = true;
    return true;
}

void output_device_uninit(void) {
    if (s_device_initialized) {
        ma_device_uninit(&s_device);
        ma_pcm_rb_uninit(&s_ring_buffer);
        s_device_initialized = false;
    }
}

bool output_device_is_active(void) {
    return s_device_initialized && (ma_device_get_state(&s_device) == ma_device_state_started);
}

void output_device_start(void) {
    if (s_device_initialized && ma_device_get_state(&s_device) != ma_device_state_started) {
        ma_device_start(&s_device);
    }
}

void output_device_stop(void) {
    if (s_device_initialized && ma_device_get_state(&s_device) == ma_device_state_started) {
        ma_device_stop(&s_device);
    }
}

void output_device_reset_buffer(void) {
    if (s_device_initialized) {
        ma_pcm_rb_reset(&s_ring_buffer);
    }
}

uint32_t output_device_available_write(void) {
    return s_device_initialized ? ma_pcm_rb_available_write(&s_ring_buffer) : 0;
}

uint32_t output_device_available_read(void) {
    return s_device_initialized ? ma_pcm_rb_available_read(&s_ring_buffer) : 0;
}

uint32_t output_device_write(const float *pcm_interleaved_float, uint32_t num_frames, uint16_t channels) {
    if (!s_device_initialized || num_frames == 0) return 0;
    ma_uint32 framesToWrite = num_frames;
    void *pWriteBuffer = NULL;

    ma_pcm_rb_acquire_write(&s_ring_buffer, &framesToWrite, &pWriteBuffer);
    if (framesToWrite > 0 && pWriteBuffer) {
        memcpy(pWriteBuffer, pcm_interleaved_float, framesToWrite * sizeof(float) * channels);
        ma_pcm_rb_commit_write(&s_ring_buffer, framesToWrite);
    }
    return (uint32_t)framesToWrite;
}

void output_device_arm_gapless(uint32_t boundary_frames) {
    atomic_store(&s_gapless_switched, false);
    atomic_store(&s_gapless_countdown, boundary_frames > 0 ? boundary_frames : 1);
}

bool output_device_check_gapless_switched(void) {
    if (atomic_load(&s_gapless_switched)) {
        atomic_store(&s_gapless_switched, false);
        return true;
    }
    return false;
}

void output_device_clear_gapless(void) {
    atomic_store(&s_gapless_countdown, 0);
    atomic_store(&s_gapless_switched, false);
}