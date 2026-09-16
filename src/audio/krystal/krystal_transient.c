#define _DEFAULT_SOURCE
#include "krystal_transient.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

void krystal_transient_init(KrystalTransientState *state, uint32_t sample_rate) {
    if (!state) return;
    memset(state, 0, sizeof(KrystalTransientState));
    state->sample_rate = sample_rate ? sample_rate : 44100;
    for (int c = 0; c < KRYSTAL_MAX_CHANNELS; c++) {
        state->gain_smoothed[c] = 1.0f;
    }
}

void krystal_transient_reset(KrystalTransientState *state) {
    if (!state) return;
    memset(state->env_fast, 0, sizeof(state->env_fast));
    memset(state->env_slow, 0, sizeof(state->env_slow));
    for (int c = 0; c < KRYSTAL_MAX_CHANNELS; c++) {
        state->gain_smoothed[c] = 1.0f;
    }
}

void krystal_declip_process(float *samples, uint32_t num_frames, uint16_t channels, float threshold) {
    if (!samples || num_frames < 4 || channels == 0) return;
    if (threshold < 0.80f) threshold = 0.80f;
    if (threshold > 0.999f) threshold = 0.999f;

    for (uint16_t c = 0; c < channels; c++) {
        uint32_t f = 1;
        while (f < num_frames - 2) {
            float s = samples[f * channels + c];
            float abs_s = fabsf(s);

            if (abs_s >= threshold) {
                uint32_t start = f;
                while (f < num_frames - 2 && fabsf(samples[f * channels + c]) >= threshold && (f - start) < 12) {
                    f++;
                }
                uint32_t end = f;
                uint32_t run_len = end - start;

                // Only reconstruct isolated peak clips (1 to 10 samples) 
                if (run_len >= 1 && run_len <= 10 && start >= 2 && end + 1 < num_frames) {
                    float y0 = samples[(start - 1) * channels + c];
                    float ym1 = samples[(start - 2) * channels + c];
                    float y1 = samples[end * channels + c];
                    float yp1 = samples[(end + 1) * channels + c];

                    float m0 = (y0 - ym1);
                    float m1 = (yp1 - y1);
                    float total_span = (float)(run_len + 1);

                    for (uint32_t k = 0; k < run_len; k++) {
                        float t = (float)(k + 1) / total_span;
                        float t2 = t * t;
                        float t3 = t2 * t;

                        // Cubic Hermite basis functions 
                        float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
                        float h10 = t3 - 2.0f * t2 + t;
                        float h01 = -2.0f * t3 + 3.0f * t2;
                        float h11 = t3 - t2;

                        float interp = h00 * y0 + h10 * m0 * total_span + h01 * y1 + h11 * m1 * total_span;
                        samples[(start + k) * channels + c] = interp;
                    }
                }
            } else {
                f++;
            }
        }
    }
}

float krystal_transient_process(KrystalTransientState *state, float *samples, uint32_t num_frames,
                                uint16_t num_channels, const KrystalTransientConfig *cfg,
                                uint32_t sample_rate) {
    if (!state || !samples || num_frames == 0 || num_channels == 0 || !cfg || !cfg->enabled) {
        return 0.0f;
    }

    state->sample_rate = sample_rate;
    uint16_t channels = (num_channels <= KRYSTAL_MAX_CHANNELS) ? num_channels : KRYSTAL_MAX_CHANNELS;

    // Fast envelope, 1.5ms attack, 18ms release 
    float att_fast = 1.0f - expf(-1.0f / ((float)sample_rate * 0.0015f));
    float rel_fast = 1.0f - expf(-1.0f / ((float)sample_rate * 0.018f));

    // Slow envelope, 15ms attack, 75ms release 
    float att_slow = 1.0f - expf(-1.0f / ((float)sample_rate * 0.015f));
    float rel_slow = 1.0f - expf(-1.0f / ((float)sample_rate * 0.075f));

    float param_smooth = 1.0f - expf(-1.0f / ((float)sample_rate * 0.020f));
    float gain_smooth  = 1.0f - expf(-1.0f / ((float)sample_rate * 0.003f));

    float sum_trans_energy = 0.0f;

    for (uint32_t f = 0; f < num_frames; f++) {
        state->smoothed_attack  += param_smooth * (cfg->attack  - state->smoothed_attack);
        state->smoothed_sustain += param_smooth * (cfg->sustain - state->smoothed_sustain);
        state->smoothed_mix     += param_smooth * (cfg->mix     - state->smoothed_mix);

        uint32_t base = f * num_channels;

        for (uint16_t c = 0; c < channels; c++) {
            float in = samples[base + c];
            float abs_in = fabsf(in);

            // Dual envelope followers 
            state->env_fast[c] += (abs_in > state->env_fast[c] ? att_fast : rel_fast) * (abs_in - state->env_fast[c]);
            state->env_slow[c] += (abs_in > state->env_slow[c] ? att_slow : rel_slow) * (abs_in - state->env_slow[c]);

            float diff = state->env_fast[c] - state->env_slow[c];
            if (diff < 0.0f) diff = 0.0f;
            sum_trans_energy += diff;

            // Differential gain modifiers 
            float norm_trans = diff / (state->env_fast[c] + 0.008f);
            float norm_sus   = state->env_slow[c] / (state->env_fast[c] + 0.012f);

            float target_gain = 1.0f + (state->smoothed_attack * 1.6f * norm_trans)
                                     + (state->smoothed_sustain * 0.9f * (norm_sus - 0.5f));

            if (target_gain < 0.10f) target_gain = 0.10f;
            if (target_gain > 2.50f) target_gain = 2.50f;

            state->gain_smoothed[c] += gain_smooth * (target_gain - state->gain_smoothed[c]);

            float processed = in * state->gain_smoothed[c];
            samples[base + c] = in + state->smoothed_mix * (processed - in);
        }
    }

    return sum_trans_energy / (float)(num_frames * channels);
}