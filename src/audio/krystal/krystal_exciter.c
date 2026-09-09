#define _DEFAULT_SOURCE
#include "krystal_exciter.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void biquad_set_highpass_exciter(KrystalBiquad *b, float fc, float q, float fs) {
    if (fc <= 1000.0f) fc = 1000.0f;
    if (fc >= fs * 0.45f) fc = fs * 0.45f;
    float w0 = 2.0f * (float)M_PI * (fc / fs);
    float alpha = sinf(w0) / (2.0f * q);
    float cos_w = cosf(w0);

    float a0 = 1.0f + alpha;
    b->b0 = ((1.0f + cos_w) * 0.5f) / a0;
    b->b1 = -(1.0f + cos_w) / a0;
    b->b2 = ((1.0f + cos_w) * 0.5f) / a0;
    b->a1 = (-2.0f * cos_w) / a0;
    b->a2 = (1.0f - alpha) / a0;
}

static inline float biquad_step_ch(KrystalBiquad *b, float in, uint16_t c) {
    float out = b->b0 * in + b->s1[c];
    b->s1[c] = b->b1 * in - b->a1 * out + b->s2[c];
    b->s2[c] = b->b2 * in - b->a2 * out;
    if (fabsf(out) < 1.0e-15f) out = 0.0f;
    return out;
}

void krystal_exciter_init(KrystalExciterState *state, uint32_t sample_rate) {
    if (!state) return;
    memset(state, 0, sizeof(KrystalExciterState));
    state->sample_rate = sample_rate ? sample_rate : 44100;
    state->last_cutoff = 5500.0f;
    biquad_set_highpass_exciter(&state->hp_air, state->last_cutoff, 0.7071f, (float)state->sample_rate);
    biquad_set_highpass_exciter(&state->hp_shimmer, 10500.0f, 0.7071f, (float)state->sample_rate);
}

void krystal_exciter_reset(KrystalExciterState *state) {
    if (!state) return;
    memset(state->hp_air.s1, 0, sizeof(state->hp_air.s1));
    memset(state->hp_air.s2, 0, sizeof(state->hp_air.s2));
    memset(state->hp_shimmer.s1, 0, sizeof(state->hp_shimmer.s1));
    memset(state->hp_shimmer.s2, 0, sizeof(state->hp_shimmer.s2));
    memset(state->env, 0, sizeof(state->env));
}

float krystal_exciter_process(KrystalExciterState *state, float *samples, uint32_t num_frames,
                              uint16_t num_channels, const KrystalExciterConfig *cfg, uint32_t sample_rate) {
    if (!state || !samples || num_frames == 0 || num_channels == 0 || !cfg || !cfg->enabled) {
        return 0.0f;
    }

    if (sample_rate != state->sample_rate || fabsf(cfg->cutoff_hz - state->last_cutoff) > 1.0f) {
        state->sample_rate = sample_rate;
        state->last_cutoff = cfg->cutoff_hz;
        biquad_set_highpass_exciter(&state->hp_air, cfg->cutoff_hz, 0.7071f, (float)sample_rate);
        biquad_set_highpass_exciter(&state->hp_shimmer, 10500.0f, 0.7071f, (float)sample_rate);
    }

    uint16_t channels = (num_channels <= KRYSTAL_MAX_CHANNELS) ? num_channels : KRYSTAL_MAX_CHANNELS;
    float smooth_alpha = 1.0f - expf(-1.0f / ((float)sample_rate * 0.020f));

    float att_sec = (cfg->attack_ms > 0.1f) ? (cfg->attack_ms * 1e-3f) : 0.0015f;
    float env_attack = 1.0f - expf(-1.0f / ((float)sample_rate * att_sec));
    float env_release = 1.0f - expf(-1.0f / ((float)sample_rate * 0.035f));

    float sum_exc_sq = 0.0f;

    for (uint32_t f = 0; f < num_frames; f++) {
        state->smoothed_drive += smooth_alpha * (cfg->drive - state->smoothed_drive);
        state->smoothed_mix += smooth_alpha * (cfg->mix - state->smoothed_mix);
        state->smoothed_shimmer += smooth_alpha * (cfg->shimmer - state->smoothed_shimmer);

        uint32_t base = f * num_channels;
        for (uint16_t c = 0; c < channels; c++) {
            float dry = samples[base + c];

            float high = biquad_step_ch(&state->hp_air, dry, c);
            float shimmer = biquad_step_ch(&state->hp_shimmer, dry, c);

            // Fast envelope follower
            float abs_h = fabsf(high);
            if (abs_h > state->env[c]) {
                state->env[c] += env_attack * (abs_h - state->env[c]);
            } else {
                state->env[c] += env_release * (abs_h - state->env[c]);
            }

            // Smooth downward expander, zero excitation during silence/low noise to prevent boosting hiss
            float expander = state->env[c] / (state->env[c] + 0.006f);

            // Dual-Stage Analog Tube/Tape Saturation
            float drive = 1.0f + state->smoothed_drive * 3.5f;
            float x = high * drive;
            float sat = tanhf(x + 0.12f * x * fabsf(x)) - high * 0.90f;

            // Ultra-Air 11kHz+ Shimmer Silk
            float s_drive = 1.0f + state->smoothed_drive * 2.0f;
            float s_in = shimmer * s_drive;
            float sat_shim = (tanhf(s_in) - s_in * 0.50f) * state->smoothed_shimmer * 0.50f;

            float excited = (sat * state->smoothed_mix * 0.40f + sat_shim) * expander;

            samples[base + c] = dry + excited;
            sum_exc_sq += excited * excited;
        }
    }

    uint32_t total_samples = num_frames * channels;
    return total_samples > 0 ? sqrtf(sum_exc_sq / (float)total_samples) : 0.0f;
}