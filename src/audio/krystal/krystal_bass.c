#define _DEFAULT_SOURCE
#include "krystal_bass.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void biquad_set_lowpass(KrystalBiquad *b, float fc, float q, float fs) {
    if (fc <= 10.0f) fc = 10.0f;
    if (fc >= fs * 0.45f) fc = fs * 0.45f;
    float w0 = 2.0f * (float)M_PI * (fc / fs);
    float alpha = sinf(w0) / (2.0f * q);
    float cos_w = cosf(w0);
    float a0 = 1.0f + alpha;

    b->b0 = ((1.0f - cos_w) * 0.5f) / a0;
    b->b1 = (1.0f - cos_w) / a0;
    b->b2 = ((1.0f - cos_w) * 0.5f) / a0;
    b->a1 = (-2.0f * cos_w) / a0;
    b->a2 = (1.0f - alpha) / a0;
}

static void biquad_set_highpass(KrystalBiquad *b, float fc, float q, float fs) {
    if (fc <= 5.0f) fc = 5.0f;
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

static inline float biquad_step(KrystalBiquad *b, float in, uint16_t c) {
    float out = b->b0 * in + b->s1[c];
    b->s1[c] = b->b1 * in - b->a1 * out + b->s2[c];
    b->s2[c] = b->b2 * in - b->a2 * out;
    if (fabsf(out) < 1.0e-15f) out = 0.0f;
    return out;
}

static void recalculate_filters(KrystalBassState *state, float cutoff, float rumble_hz, uint32_t fs) {
    state->sample_rate = fs;
    state->last_cutoff = cutoff;
    state->last_rumble = rumble_hz;

    // 4th-order Linkwitz-Riley low-pass for sub
    biquad_set_lowpass(&state->lp_sub1, cutoff, 0.7071f, (float)fs);
    biquad_set_lowpass(&state->lp_sub2, cutoff, 0.7071f, (float)fs);

    // 4th-order Linkwitz-Riley high-pass for mid/high complement
    biquad_set_highpass(&state->hp_mid1, cutoff, 0.7071f, (float)fs);
    biquad_set_highpass(&state->hp_mid2, cutoff, 0.7071f, (float)fs);

    float rumble = (rumble_hz >= 10.0f && rumble_hz <= 40.0f) ? rumble_hz : 20.0f;
    biquad_set_highpass(&state->hp_sub, rumble, 0.7071f, (float)fs);

    biquad_set_highpass(&state->hp_harm, cutoff * 0.90f, 0.60f, (float)fs);
    biquad_set_lowpass(&state->lp_harm,  cutoff * 3.40f, 0.60f, (float)fs);

    // Deep sub-harmonic isolation filter (< 45 Hz)
    biquad_set_lowpass(&state->lp_sub_oct, cutoff * 0.55f, 0.7071f, (float)fs);
}

void krystal_bass_init(KrystalBassState *state, uint32_t sample_rate) {
    if (!state) return;
    memset(state, 0, sizeof(KrystalBassState));
    recalculate_filters(state, 75.0f, 20.0f, sample_rate ? sample_rate : 44100);
}

void krystal_bass_reset(KrystalBassState *state) {
    if (!state) return;
    memset(state->lp_sub1.s1, 0, sizeof(state->lp_sub1.s1));
    memset(state->lp_sub1.s2, 0, sizeof(state->lp_sub1.s2));
    memset(state->lp_sub2.s1, 0, sizeof(state->lp_sub2.s1));
    memset(state->lp_sub2.s2, 0, sizeof(state->lp_sub2.s2));
    memset(state->hp_mid1.s1, 0, sizeof(state->hp_mid1.s1));
    memset(state->hp_mid1.s2, 0, sizeof(state->hp_mid1.s2));
    memset(state->hp_mid2.s1, 0, sizeof(state->hp_mid2.s1));
    memset(state->hp_mid2.s2, 0, sizeof(state->hp_mid2.s2));
    memset(state->hp_sub.s1, 0, sizeof(state->hp_sub.s1));
    memset(state->hp_sub.s2, 0, sizeof(state->hp_sub.s2));
    memset(state->hp_harm.s1, 0, sizeof(state->hp_harm.s1));
    memset(state->hp_harm.s2, 0, sizeof(state->hp_harm.s2));
    memset(state->lp_harm.s1, 0, sizeof(state->lp_harm.s1));
    memset(state->lp_harm.s2, 0, sizeof(state->lp_harm.s2));
    memset(state->lp_sub_oct.s1, 0, sizeof(state->lp_sub_oct.s1));
    memset(state->lp_sub_oct.s2, 0, sizeof(state->lp_sub_oct.s2));
    memset(state->env, 0, sizeof(state->env));
    memset(state->dc_state, 0, sizeof(state->dc_state));
}

float krystal_bass_process(KrystalBassState *state, float *samples, uint32_t num_frames,
                           uint16_t num_channels, const KrystalBassConfig *cfg, uint32_t sample_rate,
                           float *out_sub_energy, float *out_octave_energy) {
    if (!state || !samples || num_frames == 0 || num_channels == 0 || !cfg || !cfg->enabled) {
        if (out_sub_energy) *out_sub_energy = 0.0f;
        if (out_octave_energy) *out_octave_energy = 0.0f;
        return 0.0f;
    }

    if (sample_rate != state->sample_rate ||
        fabsf(cfg->cutoff_hz - state->last_cutoff) > 1.0f ||
        fabsf(cfg->rumble_hz - state->last_rumble) > 1.0f) {
        recalculate_filters(state, cfg->cutoff_hz, cfg->rumble_hz, sample_rate);
    }

    uint16_t channels = (num_channels <= KRYSTAL_MAX_CHANNELS) ? num_channels : KRYSTAL_MAX_CHANNELS;
    float smooth_alpha = 1.0f - expf(-1.0f / ((float)sample_rate * 0.020f));

    float env_attack  = 1.0f - expf(-1.0f / ((float)sample_rate * 0.0015f));
    float env_release = 1.0f - expf(-1.0f / ((float)sample_rate * 0.030f));

    float sum_harm_sq = 0.0f;
    float sum_sub_sq = 0.0f;
    float sum_oct_sq = 0.0f;

    for (uint32_t f = 0; f < num_frames; f++) {
        state->smoothed_mix        += smooth_alpha * (cfg->mix - state->smoothed_mix);
        state->smoothed_intensity  += smooth_alpha * (cfg->intensity - state->smoothed_intensity);
        state->smoothed_sub_weight += smooth_alpha * (cfg->sub_weight - state->smoothed_sub_weight);
        state->smoothed_tone       += smooth_alpha * (cfg->harmonic_tone - state->smoothed_tone);
        state->smoothed_sub_octave += smooth_alpha * (cfg->sub_octave - state->smoothed_sub_octave);
        state->smoothed_phase_deg  += smooth_alpha * (cfg->sub_phase_deg - state->smoothed_phase_deg);

        uint32_t base = f * num_channels;

        for (uint16_t c = 0; c < channels; c++) {
            float dry = samples[base + c];

            // Linkwitz-Riley 4th-order Crossover
            float sub = biquad_step(&state->lp_sub1, dry, c);
            sub = biquad_step(&state->lp_sub2, sub, c);

            float mid_high = biquad_step(&state->hp_mid1, dry, c);
            mid_high = biquad_step(&state->hp_mid2, mid_high, c);

            // Clean infrasonic rumble filter on sub
            sub = biquad_step(&state->hp_sub, sub, c);

            // Track sub envelope
            float abs_s = fabsf(sub);
            state->env[c] += (abs_s > state->env[c] ? env_attack : env_release) * (abs_s - state->env[c]);
            float env = state->env[c];
            sum_sub_sq += sub * sub;

            float harm = 0.0f;
            if (env > 1.0e-5f && state->smoothed_mix > 0.001f) {
                float norm_sub = sub / (env + 1.0e-4f);
                if (norm_sub > 2.0f) norm_sub = 2.0f;
                else if (norm_sub < -2.0f) norm_sub = -2.0f;

                // 2nd harmonic (octave warmth) 
                float x2 = norm_sub * 1.35f;
                float h2 = tanhf(x2) * fabsf(tanhf(x2)) - 0.45f * tanhf(x2);

                // 3rd harmonic (punch/attack) 
                float x3 = norm_sub * 1.55f;
                float h3 = tanhf(x3) - norm_sub * 0.60f;

                float h_blend = (1.0f - state->smoothed_tone) * h2 + state->smoothed_tone * h3;
                float drive_gain = 1.0f + state->smoothed_intensity * 2.0f;
                float raw_harm = h_blend * env * drive_gain;

                // DC filter 
                float dc_in = raw_harm;
                float dc_out = dc_in - state->dc_state[c] + 0.995f * state->dc_state[c];
                state->dc_state[c] = dc_in;

                harm = biquad_step(&state->hp_harm, dc_out, c);
                harm = biquad_step(&state->lp_harm, harm, c);
                harm *= state->smoothed_mix * 0.45f;
            }

            /* Isolates the deepest fundamental (< 45 Hz) and applies
             * hyperbolic saturation to generate massive weight */
            float sub_octave_out = 0.0f;
            if (state->smoothed_sub_octave > 0.001f) {
                float deep_sub = biquad_step(&state->lp_sub_oct, sub, c);
                float sat_deep = tanhf(deep_sub * 1.5f);
                sub_octave_out = sat_deep * state->smoothed_sub_octave * 0.65f;
                sum_oct_sq += sub_octave_out * sub_octave_out;
            }

            // Clean sub gain, direct boost in-phase with mid_high 
            float sub_out = sub * (1.0f + state->smoothed_sub_weight * 0.85f) + sub_octave_out;

            // In-phase master reconstruction without cancellation or digital crackling 
            samples[base + c] = mid_high + sub_out + harm;
            sum_harm_sq += harm * harm;
        }
    }

    uint32_t total_samples = num_frames * channels;
    if (out_sub_energy) {
        *out_sub_energy = total_samples > 0 ? sqrtf(sum_sub_sq / (float)total_samples) : 0.0f;
    }
    if (out_octave_energy) {
        *out_octave_energy = total_samples > 0 ? sqrtf(sum_oct_sq / (float)total_samples) : 0.0f;
    }
    return total_samples > 0 ? sqrtf(sum_harm_sq / (float)total_samples) : 0.0f;
}