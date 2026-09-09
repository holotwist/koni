#define _DEFAULT_SOURCE
#include "krystal_spectral.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SUB_BLOCK_SIZE 32

static void biquad_set_peak(KrystalBiquad *b, float fc, float gain_db, float q, float fs) {
    if (fabsf(gain_db) < 0.05f) {
        b->b0 = 1.0f; b->b1 = 0.0f; b->b2 = 0.0f;
        b->a1 = 0.0f; b->a2 = 0.0f;
        return;
    }
    if (fc <= 20.0f) fc = 20.0f;
    if (fc >= fs * 0.45f) fc = fs * 0.45f;

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

static void biquad_set_bandpass(KrystalBiquad *b, float fc, float q, float fs) {
    if (fc <= 20.0f) fc = 20.0f;
    if (fc >= fs * 0.45f) fc = fs * 0.45f;
    float w0 = 2.0f * (float)M_PI * (fc / fs);
    float alpha = sinf(w0) / (2.0f * q);
    float cos_w = cosf(w0);
    float a0 = 1.0f + alpha;

    b->b0 = alpha / a0;
    b->b1 = 0.0f;
    b->b2 = -alpha / a0;
    b->a1 = (-2.0f * cos_w) / a0;
    b->a2 = (1.0f - alpha) / a0;
}

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

static inline float biquad_step(KrystalBiquad *b, float in, uint16_t c) {
    float out = b->b0 * in + b->s1[c];
    b->s1[c] = b->b1 * in - b->a1 * out + b->s2[c];
    b->s2[c] = b->b2 * in - b->a2 * out;
    if (fabsf(out) < 1.0e-15f) out = 0.0f;
    return out;
}

void krystal_spectral_init(KrystalSpectralState *state, uint32_t sample_rate) {
    if (!state) return;
    memset(state, 0, sizeof(KrystalSpectralState));
    state->sample_rate = sample_rate ? sample_rate : 44100;
    state->last_harsh_fc = 3200.0f;
    state->last_boom_fc  = 160.0f;

    biquad_set_shelf(&state->tilt_low,  1000.0f, 0.0f, false, (float)state->sample_rate);
    biquad_set_shelf(&state->tilt_high, 1000.0f, 0.0f, true,  (float)state->sample_rate);
    biquad_set_bandpass(&state->sc_harsh, state->last_harsh_fc, 1.8f, (float)state->sample_rate);
    biquad_set_peak(&state->dyn_harsh,   state->last_harsh_fc, 0.0f, 1.4f, (float)state->sample_rate);
    biquad_set_bandpass(&state->sc_boom,  state->last_boom_fc,  1.4f, (float)state->sample_rate);
    biquad_set_peak(&state->dyn_boom,    state->last_boom_fc,  0.0f, 1.2f, (float)state->sample_rate);
}

void krystal_spectral_reset(KrystalSpectralState *state) {
    if (!state) return;
    memset(state->tilt_low.s1, 0, sizeof(state->tilt_low.s1));
    memset(state->tilt_low.s2, 0, sizeof(state->tilt_low.s2));
    memset(state->tilt_high.s1, 0, sizeof(state->tilt_high.s1));
    memset(state->tilt_high.s2, 0, sizeof(state->tilt_high.s2));
    memset(state->sc_harsh.s1, 0, sizeof(state->sc_harsh.s1));
    memset(state->sc_harsh.s2, 0, sizeof(state->sc_harsh.s2));
    memset(state->dyn_harsh.s1, 0, sizeof(state->dyn_harsh.s1));
    memset(state->dyn_harsh.s2, 0, sizeof(state->dyn_harsh.s2));
    memset(state->sc_boom.s1, 0, sizeof(state->sc_boom.s1));
    memset(state->sc_boom.s2, 0, sizeof(state->sc_boom.s2));
    memset(state->dyn_boom.s1, 0, sizeof(state->dyn_boom.s1));
    memset(state->dyn_boom.s2, 0, sizeof(state->dyn_boom.s2));
    memset(state->env_harsh, 0, sizeof(state->env_harsh));
    memset(state->env_boom, 0, sizeof(state->env_boom));
}

void krystal_spectral_process(KrystalSpectralState *state, float *samples, uint32_t num_frames,
                              uint16_t num_channels, const KrystalSpectralConfig *cfg,
                              uint32_t sample_rate, float *out_harsh_cut_db, float *out_boom_cut_db) {
    if (!state || !samples || num_frames == 0 || num_channels == 0 || !cfg || !cfg->enabled) {
        if (out_harsh_cut_db) *out_harsh_cut_db = 0.0f;
        if (out_boom_cut_db)  *out_boom_cut_db  = 0.0f;
        return;
    }

    float harsh_fc = cfg->harsh_freq > 1000.0f ? cfg->harsh_freq : 3200.0f;
    float boom_fc  = cfg->boom_freq  > 40.0f   ? cfg->boom_freq  : 160.0f;

    if (sample_rate != state->sample_rate ||
        fabsf(harsh_fc - state->last_harsh_fc) > 10.0f ||
        fabsf(boom_fc - state->last_boom_fc) > 5.0f) {
        state->sample_rate = sample_rate;
        state->last_harsh_fc = harsh_fc;
        state->last_boom_fc  = boom_fc;
        biquad_set_bandpass(&state->sc_harsh, harsh_fc, 1.8f, (float)sample_rate);
        biquad_set_bandpass(&state->sc_boom,  boom_fc,  1.4f, (float)sample_rate);
    }

    // Tilt filter update 
    if (fabsf(cfg->tilt_db - state->current_tilt_db) > 0.05f) {
        state->current_tilt_db = cfg->tilt_db;
        biquad_set_shelf(&state->tilt_low,  1000.0f, -cfg->tilt_db * 0.5f, false, (float)sample_rate);
        biquad_set_shelf(&state->tilt_high, 1000.0f, +cfg->tilt_db * 0.5f, true,  (float)sample_rate);
    }

    uint16_t channels = (num_channels <= KRYSTAL_MAX_CHANNELS) ? num_channels : KRYSTAL_MAX_CHANNELS;
    float att_alpha = 1.0f - expf(-1.0f / ((float)sample_rate * 0.005f));  // 5ms attack 
    float rel_alpha = 1.0f - expf(-1.0f / ((float)sample_rate * 0.045f));  // 45ms release 

    const float thresh_harsh = 0.220f; // -13 dBFS detection threshold 
    const float thresh_boom  = 0.300f; // -10 dBFS detection threshold 

    uint32_t frames_done = 0;
    while (frames_done < num_frames) {
        uint32_t block = num_frames - frames_done;
        if (block > SUB_BLOCK_SIZE) block = SUB_BLOCK_SIZE;

        float max_env_harsh = 0.0f;
        float max_env_boom  = 0.0f;

        // Sidechain detection pass 
        for (uint32_t f = 0; f < block; f++) {
            uint32_t base = (frames_done + f) * num_channels;
            for (uint16_t c = 0; c < channels; c++) {
                float s = samples[base + c];

                float h_sc = fabsf(biquad_step(&state->sc_harsh, s, c));
                state->env_harsh[c] += (h_sc > state->env_harsh[c] ? att_alpha : rel_alpha) * (h_sc - state->env_harsh[c]);
                if (state->env_harsh[c] > max_env_harsh) max_env_harsh = state->env_harsh[c];

                float b_sc = fabsf(biquad_step(&state->sc_boom, s, c));
                state->env_boom[c] += (b_sc > state->env_boom[c] ? att_alpha : rel_alpha) * (b_sc - state->env_boom[c]);
                if (state->env_boom[c] > max_env_boom) max_env_boom = state->env_boom[c];
            }
        }

        // Calculate dynamic reduction with musical limits 
        float target_harsh_cut = 0.0f;
        if (cfg->de_harsh > 0.001f && max_env_harsh > thresh_harsh) {
            float excess = (max_env_harsh - thresh_harsh) / thresh_harsh;
            target_harsh_cut = -excess * cfg->de_harsh * 3.5f;
            if (target_harsh_cut < -4.0f) target_harsh_cut = -4.0f;
        }

        float target_boom_cut = 0.0f;
        if (cfg->de_boom > 0.001f && max_env_boom > thresh_boom) {
            float excess = (max_env_boom - thresh_boom) / thresh_boom;
            target_boom_cut = -excess * cfg->de_boom * 3.5f;
            if (target_boom_cut < -4.0f) target_boom_cut = -4.0f;
        }

        // Smooth biquad gain updates 
        state->current_harsh_cut_db += 0.25f * (target_harsh_cut - state->current_harsh_cut_db);
        state->current_boom_cut_db  += 0.25f * (target_boom_cut  - state->current_boom_cut_db);

        biquad_set_peak(&state->dyn_harsh, state->last_harsh_fc, state->current_harsh_cut_db, 1.4f, (float)sample_rate);
        biquad_set_peak(&state->dyn_boom,  state->last_boom_fc,  state->current_boom_cut_db,  1.2f, (float)sample_rate);

        // Processing pass 
        for (uint32_t f = 0; f < block; f++) {
            uint32_t base = (frames_done + f) * num_channels;
            for (uint16_t c = 0; c < channels; c++) {
                float s = samples[base + c];
                s = biquad_step(&state->tilt_low,  s, c);
                s = biquad_step(&state->tilt_high, s, c);
                s = biquad_step(&state->dyn_harsh, s, c);
                s = biquad_step(&state->dyn_boom,  s, c);
                samples[base + c] = s;
            }
        }

        frames_done += block;
    }

    if (out_harsh_cut_db) *out_harsh_cut_db = state->current_harsh_cut_db;
    if (out_boom_cut_db)  *out_boom_cut_db  = state->current_boom_cut_db;
}