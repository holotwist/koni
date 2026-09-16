#define _DEFAULT_SOURCE
#include "krystal_loudness.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void biquad_set_shelf(KrystalBiquad *b, float fc, float gain_db, bool is_high, float fs) {
    if (fabsf(gain_db) < 0.05f) {
        b->b0 = 1.0f; b->b1 = 0.0f; b->b2 = 0.0f;
        b->a1 = 0.0f; b->a2 = 0.0f;
        return;
    }
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * (float)M_PI * (fc / fs);
    float cos_w = cosf(w0);
    float sin_w = sinf(w0);
    float alpha = sin_w * 0.5f * sqrtf((A + 1.0f / A) * (1.0f / 0.7071f - 1.0f) + 2.0f);

    float b0, b1, b2, a0, a1, a2;
    if (!is_high) {
        b0 =    A * ((A + 1.0f) - (A - 1.0f) * cos_w + 2.0f * sqrtf(A) * alpha);
        b1 =  2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_w);
        b2 =    A * ((A + 1.0f) - (A - 1.0f) * cos_w - 2.0f * sqrtf(A) * alpha);
        a0 =         (A + 1.0f) + (A - 1.0f) * cos_w + 2.0f * sqrtf(A) * alpha;
        a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cos_w);
        a2 =         (A + 1.0f) + (A - 1.0f) * cos_w - 2.0f * sqrtf(A) * alpha;
    } else {
        b0 =    A * ((A + 1.0f) + (A - 1.0f) * cos_w + 2.0f * sqrtf(A) * alpha);
        b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_w);
        b2 =    A * ((A + 1.0f) + (A - 1.0f) * cos_w - 2.0f * sqrtf(A) * alpha);
        a0 =         (A + 1.0f) - (A - 1.0f) * cos_w + 2.0f * sqrtf(A) * alpha;
        a1 =  2.0f * ((A - 1.0f) - (A + 1.0f) * cos_w);
        a2 =         (A + 1.0f) - (A - 1.0f) * cos_w - 2.0f * sqrtf(A) * alpha;
    }

    b->b0 = b0 / a0; b->b1 = b1 / a0; b->b2 = b2 / a0;
    b->a1 = a1 / a0; b->a2 = a2 / a0;
}

static void biquad_set_peak(KrystalBiquad *b, float fc, float gain_db, float q, float fs) {
    if (fabsf(gain_db) < 0.05f) {
        b->b0 = 1.0f; b->b1 = 0.0f; b->b2 = 0.0f;
        b->a1 = 0.0f; b->a2 = 0.0f;
        return;
    }
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * (float)M_PI * (fc / fs);
    float alpha = sinf(w0) / (2.0f * q);
    float cos_w = cosf(w0);

    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cos_w;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cos_w;
    float a2 = 1.0f - alpha / A;

    b->b0 = b0 / a0; b->b1 = b1 / a0; b->b2 = b2 / a0;
    b->a1 = a1 / a0; b->a2 = a2 / a0;
}

static inline float biquad_step(KrystalBiquad *b, float in, uint16_t c) {
    float out = b->b0 * in + b->s1[c];
    b->s1[c] = b->b1 * in - b->a1 * out + b->s2[c];
    b->s2[c] = b->b2 * in - b->a2 * out;
    if (fabsf(out) < 1.0e-15f) out = 0.0f;
    return out;
}

void krystal_loudness_init(KrystalLoudnessState *state, uint32_t sample_rate) {
    if (!state) return;
    memset(state, 0, sizeof(KrystalLoudnessState));
    state->sample_rate = sample_rate ? sample_rate : 44100;
    state->smoothed_volume = 1.0f;
    biquad_set_shelf(&state->low_shelf,  80.0f,   0.0f, false, (float)state->sample_rate);
    biquad_set_peak(&state->mid_dip,    2800.0f,  0.0f, 0.9f,  (float)state->sample_rate);
    biquad_set_shelf(&state->high_shelf, 8500.0f, 0.0f, true,  (float)state->sample_rate);
}

void krystal_loudness_reset(KrystalLoudnessState *state) {
    if (!state) return;
    memset(state->low_shelf.s1, 0, sizeof(state->low_shelf.s1));
    memset(state->low_shelf.s2, 0, sizeof(state->low_shelf.s2));
    memset(state->mid_dip.s1, 0, sizeof(state->mid_dip.s1));
    memset(state->mid_dip.s2, 0, sizeof(state->mid_dip.s2));
    memset(state->high_shelf.s1, 0, sizeof(state->high_shelf.s1));
    memset(state->high_shelf.s2, 0, sizeof(state->high_shelf.s2));
}

void krystal_loudness_process(KrystalLoudnessState *state, float *samples, uint32_t num_frames,
                              uint16_t num_channels, const KrystalLoudnessConfig *cfg,
                              uint32_t sample_rate, int volume_percent,
                              float *out_bass_db, float *out_treb_db) {
    if (!state || !samples || num_frames == 0 || num_channels == 0 || !cfg || !cfg->enabled || cfg->mode == LOUDNESS_MODE_OFF) {
        if (out_bass_db) *out_bass_db = 0.0f;
        if (out_treb_db) *out_treb_db = 0.0f;
        return;
    }

    state->sample_rate = sample_rate;
    float vol_norm = (float)volume_percent / 100.0f;
    if (vol_norm > 1.0f) vol_norm = 1.0f;
    if (vol_norm < 0.0f) vol_norm = 0.0f;

    float vol_smooth_alpha = 1.0f - expf(-1.0f / ((float)sample_rate * 0.050f));
    state->smoothed_volume += vol_smooth_alpha * (vol_norm - state->smoothed_volume);

    float deficit = 0.0f;
    if (cfg->mode == LOUDNESS_MODE_DYNAMIC) {
        float ref = (cfg->ref_vol > 0.1f) ? cfg->ref_vol : 0.80f;
        if (state->smoothed_volume < ref) {
            deficit = (ref - state->smoothed_volume) / ref;
        }
    } else {
        deficit = 0.65f;
    }

    // Calibrated ISO 226 low-end rise and ear-canal sensitivity notch 
    float target_bass_gain = deficit * 8.5f * cfg->intensity;
    float target_mid_gain  = -deficit * 1.8f * cfg->intensity;
    float target_treb_gain = deficit * 3.8f * cfg->intensity;

    if (fabsf(target_bass_gain - state->current_bass_gain) > 0.05f ||
        fabsf(target_treb_gain - state->current_treb_gain) > 0.05f) {
        state->current_bass_gain = target_bass_gain;
        state->current_mid_gain  = target_mid_gain;
        state->current_treb_gain = target_treb_gain;

        biquad_set_shelf(&state->low_shelf,  80.0f,   target_bass_gain, false, (float)sample_rate);
        biquad_set_peak(&state->mid_dip,    2800.0f,  target_mid_gain,  0.9f,  (float)sample_rate);
        biquad_set_shelf(&state->high_shelf, 8500.0f, target_treb_gain, true,  (float)sample_rate);
    }

    if (out_bass_db) *out_bass_db = state->current_bass_gain;
    if (out_treb_db) *out_treb_db = state->current_treb_gain;

    uint16_t channels = (num_channels <= KRYSTAL_MAX_CHANNELS) ? num_channels : KRYSTAL_MAX_CHANNELS;

    for (uint32_t f = 0; f < num_frames; f++) {
        uint32_t base = f * num_channels;
        for (uint16_t c = 0; c < channels; c++) {
            float s = samples[base + c];
            s = biquad_step(&state->low_shelf,  s, c);
            s = biquad_step(&state->mid_dip,    s, c);
            s = biquad_step(&state->high_shelf, s, c);
            samples[base + c] = s;
        }
    }
}