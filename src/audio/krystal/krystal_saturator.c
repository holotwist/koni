#define _DEFAULT_SOURCE
#include "krystal_saturator.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

void krystal_saturator_init(KrystalSaturatorState *state, uint32_t sample_rate) {
    if (!state) return;
    memset(state, 0, sizeof(KrystalSaturatorState));
    state->sample_rate = sample_rate ? sample_rate : 44100;
    krystal_oversampler_init(&state->oversampler);
}

void krystal_saturator_reset(KrystalSaturatorState *state) {
    if (!state) return;
    krystal_oversampler_reset(&state->oversampler);
    memset(state->dc_state, 0, sizeof(state->dc_state));
    memset(state->hysteresis, 0, sizeof(state->hysteresis));
    memset(state->tone_state, 0, sizeof(state->tone_state));
}

static inline float sat_triode(float x, float drive, float bias) {
    float in = (x + bias) * (1.0f + drive * 3.0f);
    float out;
    if (in >= 0.0f) {
        out = tanhf(in);
    } else {
        out = in / (1.0f + fabsf(in));
    }
    return out - tanhf(bias);
}

static inline float sat_pentode(float x, float drive) {
    float in = x * (1.0f + drive * 3.5f);
    float in3 = in * in * in;
    float shaped = in - 0.166667f * in3;
    return tanhf(shaped);
}

static inline float sat_tape(float x, float drive, float *hyst) {
    float in = x * (1.0f + drive * 2.5f);
    float h = *hyst;
    float delta = in - h;
    float sat = tanhf(in + 0.15f * delta);
    *hyst = h + 0.45f * (sat - h);
    return sat;
}

static inline float sat_transformer(float x, float drive, float *flux) {
    float in = x * (1.0f + drive * 3.0f);
    *flux = 0.92f * (*flux) + 0.08f * in;
    float core = in - 0.25f * (*flux);
    return tanhf(core);
}

float krystal_saturator_process(KrystalSaturatorState *state, float *samples, uint32_t num_frames,
                               uint16_t num_channels, const KrystalSatConfig *cfg, uint32_t sample_rate) {
    if (!state || !samples || num_frames == 0 || num_channels == 0 || !cfg || !cfg->enabled || cfg->mode == SAT_MODE_OFF) {
        return 0.0f;
    }

    state->sample_rate = sample_rate;
    uint16_t channels = (num_channels <= KRYSTAL_MAX_CHANNELS) ? num_channels : KRYSTAL_MAX_CHANNELS;
    float smooth_alpha = 1.0f - expf(-1.0f / ((float)sample_rate * 0.020f));

    state->smoothed_drive += smooth_alpha * (cfg->drive - state->smoothed_drive);
    state->smoothed_bias  += smooth_alpha * (cfg->bias  - state->smoothed_bias);
    state->smoothed_tone  += smooth_alpha * (cfg->tone  - state->smoothed_tone);
    state->smoothed_mix   += smooth_alpha * (cfg->mix   - state->smoothed_mix);

    bool use_os = cfg->oversample;
    uint32_t proc_frames = use_os ? (num_frames * 2) : num_frames;
    uint32_t proc_srate = use_os ? (sample_rate * 2) : sample_rate;
    float *proc_buf = samples;

    if (use_os) {
        size_t needed = (size_t)proc_frames * (size_t)channels;
        if (needed > state->buf_2x_cap) {
            free(state->buf_2x);
            state->buf_2x = malloc(needed * sizeof(float));
            state->buf_2x_cap = state->buf_2x ? needed : 0;
        }
        if (!state->buf_2x) return 0.0f;
        krystal_oversample_up(&state->oversampler, samples, state->buf_2x, num_frames, channels);
        proc_buf = state->buf_2x;
    }

    // Sub-audible DC tracking filter (10 Hz) 
    float dc_alpha = 1.0f - expf(-2.0f * (float)M_PI * 10.0f / (float)proc_srate);

    /* Tilt filter pivot at 3 kHz
     * When tone == 0.50, tilt == 0.0 and the response is 100% flat */
    float tone_alpha = 1.0f - expf(-2.0f * (float)M_PI * 3000.0f / (float)proc_srate);
    float tilt_factor = (state->smoothed_tone - 0.50f) * 2.0f; // -1.0 (warm) to +1.0 (bright) 

    float sum_sat_sq = 0.0f;

    for (uint32_t f = 0; f < proc_frames; f++) {
        uint32_t base = f * channels;
        for (uint16_t c = 0; c < channels; c++) {
            float in = proc_buf[base + c];
            float sat = in;

            switch (cfg->mode) {
                case SAT_MODE_TRIODE:
                    sat = sat_triode(in, state->smoothed_drive, state->smoothed_bias);
                    break;
                case SAT_MODE_PENTODE:
                    sat = sat_pentode(in, state->smoothed_drive);
                    break;
                case SAT_MODE_TAPE:
                    sat = sat_tape(in, state->smoothed_drive, &state->hysteresis[c]);
                    break;
                case SAT_MODE_TRANSFORMER:
                    sat = sat_transformer(in, state->smoothed_drive, &state->hysteresis[c]);
                    break;
                default:
                    break;
            }

            // Sub-audible DC offset rejection 
            state->dc_state[c] += dc_alpha * (sat - state->dc_state[c]);
            sat -= state->dc_state[c];

            // Linear-phase tilt tone shaping (neutral at 0.50) 
            state->tone_state[c] += tone_alpha * (sat - state->tone_state[c]);
            float lp = state->tone_state[c];
            float hp = sat - lp;
            sat += tilt_factor * (hp - lp) * 0.35f;

            /* In-line zero-latency dry/wet blend at the oversampled rate
             * Guarantees true parallel blend with zero phase cancellation. */
            proc_buf[base + c] = in + state->smoothed_mix * (sat - in);
            sum_sat_sq += sat * sat;
        }
    }

    if (use_os) {
        krystal_oversample_down(&state->oversampler, proc_buf, samples, num_frames, channels);
    }

    return sqrtf(sum_sat_sq / (float)(proc_frames * channels));
}