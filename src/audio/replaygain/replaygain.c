#include "replaygain.h"
#include <math.h>
#include <string.h>

// Reference targets (EBU R128 / ReplayGain 2.0 reference: -18.0 dBFS RMS target ≈ 0.12589f)
#define TARGET_RMS           0.12589f
#define TARGET_ENERGY        (TARGET_RMS * TARGET_RMS)
#define ABSOLUTE_GATE_ENERGY 1.0e-6f  // -60 dBFS
#define PEAK_CEILING         0.95000f  // -0.45 dBFS ceiling

void rgain_init(RGainState *rg, uint32_t sample_rate, uint32_t channels) {
    if (!rg) return;
    memset(rg, 0, sizeof(RGainState));
    rg->sample_rate = sample_rate ? sample_rate : 44100;
    rg->channels = channels ? channels : 2;
    rg->current_multiplier = 1.0f;
    rg->target_multiplier = 1.0f;
    rg->integrated_energy = TARGET_ENERGY;
    rg->short_term_energy = TARGET_ENERGY;
    rg->peak_energy = 0.0f;
    rg->frames_seen = 0;

    // Fast stage (300 ms) for initial track convergence
    rg->alpha_fast = 1.0f - expf(-1.0f / ((float)rg->sample_rate * 0.300f));
    // Slow stage (10.0 seconds) for transparent, non-pumping musical integration
    rg->alpha_slow = 1.0f - expf(-1.0f / ((float)rg->sample_rate * 10.0f));
    // Slew filter for gain transitions (~100 ms)
    rg->alpha_slew = 1.0f - expf(-1.0f / ((float)rg->sample_rate * 0.100f));
}

void rgain_set_meta(RGainState *rg, bool has_meta, float track_gain_db) {
    if (!rg) return;
    rg->has_meta = has_meta;
    rg->meta_gain_db = track_gain_db;
    if (rg->mode == RGAIN_META && has_meta) {
        rg->target_multiplier = powf(10.0f, track_gain_db / 20.0f);
    }
}

void rgain_set_mode(RGainState *rg, RGainMode mode) {
    if (!rg) return;
    rg->mode = mode;
    if (mode == RGAIN_META && rg->has_meta) {
        rg->target_multiplier = powf(10.0f, rg->meta_gain_db / 20.0f);
    } else if (mode == RGAIN_OFF) {
        rg->target_multiplier = 1.0f;
    } else if (mode == RGAIN_CALC) {
        // Reset dynamic tracking on mode switch
        rg->integrated_energy = TARGET_ENERGY;
        rg->short_term_energy = TARGET_ENERGY;
        rg->frames_seen = 0;
    }
}

void rgain_process_float(RGainState *rg, float *samples, uint32_t num_frames) {
    if (!rg || !samples || num_frames == 0 || rg->mode == RGAIN_OFF) return;

    // Mode 1, Static Metadata (ReplayGain tag provided)
    if (rg->mode == RGAIN_META && rg->has_meta) {
        float target = rg->target_multiplier;
        float slew = rg->alpha_slew;

        if (fabsf(rg->current_multiplier - target) < 0.0001f) {
            rg->current_multiplier = target;
            uint32_t total = num_frames * rg->channels;
            for (uint32_t i = 0; i < total; i++) {
                samples[i] *= target;
            }
        } else {
            for (uint32_t f = 0; f < num_frames; f++) {
                rg->current_multiplier += slew * (target - rg->current_multiplier);
                uint32_t base = f * rg->channels;
                for (uint32_t c = 0; c < rg->channels; c++) {
                    samples[base + c] *= rg->current_multiplier;
                }
            }
        }
        return;
    }

    // Mode 2, Real-time Auto-Calculation with Dual-Gating & Slow Integration
    if (rg->mode == RGAIN_CALC || (rg->mode == RGAIN_META && !rg->has_meta)) {
        float integ_energy = rg->integrated_energy;
        float short_energy = rg->short_term_energy;
        float current_mult = rg->current_multiplier;
        float target_mult  = rg->target_multiplier;
        float slew         = rg->alpha_slew;

        // Use fast learning rate during first 2 seconds, then lock into 10s integration
        uint32_t lock_frames = rg->sample_rate * 2;
        float alpha = (rg->frames_seen < lock_frames) ? rg->alpha_fast : rg->alpha_slow;

        for (uint32_t f = 0; f < num_frames; f++) {
            uint32_t base = f * rg->channels;
            float sum_sq = 0.0f;
            float max_abs = 0.0f;

            for (uint32_t c = 0; c < rg->channels; c++) {
                float s = samples[base + c];
                float a = fabsf(s);
                if (a > max_abs) max_abs = a;
                sum_sq += s * s;
            }

            float frame_energy = sum_sq / (float)rg->channels;

            // Gating Stage 1, Absolute silence gate (-60 dBFS)
            if (frame_energy > ABSOLUTE_GATE_ENERGY) {
                short_energy += 0.005f * (frame_energy - short_energy);

                // Gating Stage 2, Relative gate (-12 dB below short-term musical average)
                if (frame_energy > (short_energy * 0.063f)) {
                    integ_energy += alpha * (frame_energy - integ_energy);
                    rg->frames_seen++;

                    float rms = sqrtf(integ_energy);
                    if (rms > 0.001f) {
                        float desired = TARGET_RMS / rms;
                        // Limit maximum boost to +8 dB, maximum cut to -14 dB
                        if (desired > 2.50f) desired = 2.50f;
                        if (desired < 0.20f) desired = 0.20f;

                        // Ceiling clamp, Never allow multiplier to drive the current peak past ceiling
                        if (max_abs > 0.01f && (max_abs * desired) > PEAK_CEILING) {
                            desired = PEAK_CEILING / max_abs;
                        }

                        target_mult = desired;
                    }
                }
            }

            // Slew the multiplier smoothly (anti-zipper, anti-pump)
            current_mult += slew * (target_mult - current_mult);

            for (uint32_t c = 0; c < rg->channels; c++) {
                samples[base + c] *= current_mult;
            }
        }

        rg->integrated_energy = integ_energy;
        rg->short_term_energy = short_energy;
        rg->target_multiplier = target_mult;
        rg->current_multiplier = current_mult;
    }
}

void rgain_process(RGainState *rg, int32_t *pcm, uint32_t num_frames) {
    if (!rg || !pcm || num_frames == 0 || rg->mode == RGAIN_OFF) return;

    float mult = rg->current_multiplier;
    uint32_t total = num_frames * rg->channels;
    static uint32_t s_rg_rng = 0x87654321;

    for (uint32_t i = 0; i < total; i++) {
        s_rg_rng ^= s_rg_rng << 13;
        s_rg_rng ^= s_rg_rng >> 17;
        s_rg_rng ^= s_rg_rng << 5;
        float r1 = (float)(int32_t)s_rg_rng * (1.0f / 2147483648.0f);

        s_rg_rng ^= s_rg_rng << 13;
        s_rg_rng ^= s_rg_rng >> 17;
        s_rg_rng ^= s_rg_rng << 5;
        float r2 = (float)(int32_t)s_rg_rng * (1.0f / 2147483648.0f);

        double val = ((double)pcm[i] * (double)mult) + (double)(r1 - r2);
        if (val > 2147483647.0) val = 2147483647.0;
        else if (val < -2147483648.0) val = -2147483648.0;
        pcm[i] = (int32_t)round(val);
    }
}