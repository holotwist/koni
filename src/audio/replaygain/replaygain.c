#include "replaygain.h"
#include <math.h>
#include <string.h>

// Approximate -20 dBFS target for normalization (~0.1 RMS)
#define TARGET_RMS 0.1f 
#define SMOOTHING 0.00003f // Subtle continuous interpolation to avoid modulation distortion

void rgain_init(RGainState *rg, uint32_t sample_rate, uint32_t channels) {
    rg->sample_rate = sample_rate ? sample_rate : 44100;
    rg->channels = channels ? channels : 2;
    rg->current_multiplier = 1.0f;
    rg->target_multiplier = 1.0f;
    rg->avg_energy = TARGET_RMS * TARGET_RMS;
    // ~400ms time-constant leaky integrator filter
    rg->energy_alpha = 1.0f - expf(-1.0f / ((float)rg->sample_rate * 0.4f));
}

void rgain_set_meta(RGainState *rg, bool has_meta, float track_gain_db) {
    rg->has_meta = has_meta;
    rg->meta_gain_db = track_gain_db;
}

void rgain_set_mode(RGainState *rg, RGainMode mode) {
    rg->mode = mode;
    if (mode == RGAIN_META && rg->has_meta) {
        rg->target_multiplier = powf(10.0f, rg->meta_gain_db / 20.0f);
        rg->current_multiplier = rg->target_multiplier;
    } else if (mode == RGAIN_OFF) {
        rg->target_multiplier = 1.0f;
        rg->current_multiplier = 1.0f;
    } else if (mode == RGAIN_CALC) {
        rg->avg_energy = TARGET_RMS * TARGET_RMS;
    }
}

void rgain_process(RGainState *rg, int32_t *pcm, uint32_t num_frames) {
    if (rg->mode == RGAIN_OFF) return;
    
    if (rg->mode == RGAIN_META && rg->has_meta) {
        float mult = rg->current_multiplier;
        for (uint32_t i = 0; i < num_frames * rg->channels; i++) {
            long long val = (long long)((float)pcm[i] * mult);
            if (val > 2147483647LL) val = 2147483647LL;
            else if (val < -2147483648LL) val = -2147483648LL;
            pcm[i] = (int32_t)val;
        }
        return;
    }
    
    // Dynamic calculation mode
    if (rg->mode == RGAIN_CALC || (rg->mode == RGAIN_META && !rg->has_meta)) {
        float alpha = rg->energy_alpha;
        float avg_energy = rg->avg_energy;
        
        for (uint32_t i = 0; i < num_frames; i++) {
            float sum_squares = 0.0f;
            for (uint32_t c = 0; c < rg->channels; c++) {
                float sample = (float)pcm[i * rg->channels + c] / 2147483648.0f;
                sum_squares += sample * sample;
            }
            
            float frame_energy = sum_squares / (float)rg->channels;
            avg_energy += alpha * (frame_energy - avg_energy);
            
            float rms = sqrtf(avg_energy);
            if (rms > 0.001f) {
                float desired_mult = TARGET_RMS / rms;
                if (desired_mult > 3.0f) desired_mult = 3.0f; 
                if (desired_mult < 0.25f) desired_mult = 0.25f;
                rg->target_multiplier += SMOOTHING * (desired_mult - rg->target_multiplier);
            }
            
            rg->current_multiplier += SMOOTHING * 4.0f * (rg->target_multiplier - rg->current_multiplier);
            
            for (uint32_t c = 0; c < rg->channels; c++) {
                long long val = (long long)((float)pcm[i * rg->channels + c] * rg->current_multiplier);
                if (val > 2147483647LL) val = 2147483647LL;
                else if (val < -2147483648LL) val = -2147483648LL;
                pcm[i * rg->channels + c] = (int32_t)val;
            }
        }
        rg->avg_energy = avg_energy;
    }
}