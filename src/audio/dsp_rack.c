#define _DEFAULT_SOURCE
#include "dsp_rack.h"
#include "equalizer.h"
#include "state.h"
#include <stdlib.h>
#include <math.h>

#define FLOAT_CHUNK_CAPACITY (16384 * 8)
static float *s_float_buf = NULL;
static uint32_t s_dither_prng_state = 0x12345678;

void dsp_rack_init(void) {
    if (!s_float_buf) {
        s_float_buf = malloc(sizeof(float) * FLOAT_CHUNK_CAPACITY);
    }
}

/* Fast Triangular Probability Density Function (TPDF) Dither Generator */
static inline float generate_tpdf_dither(void) {
    s_dither_prng_state = s_dither_prng_state * 1664525u + 1013904223u;
    int32_t r1 = (int32_t)s_dither_prng_state;
    s_dither_prng_state = s_dither_prng_state * 1664525u + 1013904223u;
    int32_t r2 = (int32_t)s_dither_prng_state;

    // Sum of two uniform independent random variables yields triangular distribution
    return ((float)r1 + (float)r2) * (1.0f / 4294967296.0f);
}

/* Soft knee limiter */
static inline float soft_limit_sample(float x) {
    const float threshold = 0.85f;
    if (x > threshold) {
        float excess = x - threshold;
        return threshold + (1.0f - threshold) * tanhf(excess / (1.0f - threshold));
    } else if (x < -threshold) {
        float excess = -x - threshold;
        return -(threshold + (1.0f - threshold) * tanhf(excess / (1.0f - threshold)));
    }
    return x;
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
    float vol_factor = 0.0f;
    if (volume_percent > 0) {
        if (volume_percent <= 100) {
            float norm = (float)volume_percent / 100.0f;
            vol_factor = norm * norm * norm;
        } else {
            float boost = (float)(volume_percent - 100) / 100.0f;
            vol_factor = 1.0f + boost;
        }
    }

    // True-Peak Soft Limiter (tanh knee)
    for (uint32_t s = 0; s < total_samples; s++) {
        float_out[s] = soft_limit_sample(float_out[s] * vol_factor);
    }
}