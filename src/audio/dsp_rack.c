#define _DEFAULT_SOURCE
#include "dsp_rack.h"
#include "equalizer.h"
#include "dc_blocker.h"
#include "limiter.h"
#include "state.h"
#include <stdlib.h>
#include <math.h>

#define FLOAT_CHUNK_CAPACITY (16384 * 8)
static float *s_float_buf = NULL;
static float s_current_vol_factor = -1.0f; // -1 indicates uninitialized
static DCBlocker s_dc_blocker;
static LookaheadLimiter s_limiter;

void dsp_rack_init(void) {
    if (!s_float_buf) {
        s_float_buf = malloc(sizeof(float) * FLOAT_CHUNK_CAPACITY);
    }
    dc_blocker_init(&s_dc_blocker, 44100);
    limiter_init(&s_limiter, 44100);
}

void dsp_rack_reset(void) {
    dc_blocker_reset(&s_dc_blocker);
    limiter_reset(&s_limiter);
    s_current_vol_factor = -1.0f;
}

void dsp_rack_process(const int32_t *pcm_in, float *float_out, uint32_t num_frames,
                      uint16_t num_channels, uint32_t sample_rate,
                      RGainState *rgain, int rgain_mode, int volume_percent) {
    if (!pcm_in || !float_out || num_frames == 0 || num_channels == 0) return;

    uint32_t total_samples = num_frames * num_channels;

    // Initial conversion, normalize raw decoder integer to float [-1.0f, +1.0f]
    for (uint32_t s = 0; s < total_samples; s++) {
        float_out[s] = (float)pcm_in[s] * (1.0f / 2147483648.0f);
    }

    // Infrasonic DC-Offset blocker
    dc_blocker_process(&s_dc_blocker, float_out, num_frames, num_channels, sample_rate);

    // ReplayGain
    if (rgain && rgain_mode != RGAIN_OFF) {
        rgain_set_mode(rgain, (RGainMode)rgain_mode);
        rgain_process_float(rgain, float_out, num_frames);
    }

    // 10-Band Graphic Equalizer
    if (eq_is_enabled()) {
        eq_process_float(float_out, num_frames, num_channels, sample_rate);
    }

    // Visualizer Tap
    uint32_t local_wpos = atomic_load(&vis_wpos);
    for (uint32_t f = 0; f < num_frames; f++) {
        float vl = 0.0f, vr = 0.0f;
        if (num_channels == 1) {
            vl = vr = float_out[f];
        } else if (num_channels >= 2) {
            vl = float_out[f * num_channels];
            vr = float_out[f * num_channels + 1];
        }
        vis_ring_l[local_wpos & VIS_BUF_MASK] = vl;
        vis_ring_r[local_wpos & VIS_BUF_MASK] = vr;
        local_wpos++;
    }
    atomic_store(&vis_wpos, local_wpos);

    // Cubic Volume Curve
    float target_vol = 0.0f;
    if (volume_percent > 0) {
        if (volume_percent <= 100) {
            float norm = (float)volume_percent / 100.0f;
            target_vol = norm * norm * norm;
        } else {
            float boost = (float)(volume_percent - 100) / 100.0f;
            target_vol = 1.0f + boost;
        }
    }

    if (s_current_vol_factor < 0.0f) {
        s_current_vol_factor = target_vol;
    }

    float vol_alpha = 1.0f - expf(-1.0f / ((float)sample_rate * 0.015f));

    if (fabsf(s_current_vol_factor - target_vol) < 0.00001f) {
        s_current_vol_factor = target_vol;
        if (target_vol != 1.0f) {
            for (uint32_t s = 0; s < total_samples; s++) {
                float_out[s] *= target_vol;
            }
        }
    } else {
        for (uint32_t f = 0; f < num_frames; f++) {
            s_current_vol_factor += vol_alpha * (target_vol - s_current_vol_factor);
            uint32_t base = f * num_channels;
            for (uint16_t c = 0; c < num_channels; c++) {
                float_out[base + c] *= s_current_vol_factor;
            }
        }
    }

    // Lookahead True-Peak Mastering Limiter (Channel-linked, 96-sample lookahead, -0.2 dBFS ceiling)
    limiter_process(&s_limiter, float_out, num_frames, num_channels, sample_rate);
}