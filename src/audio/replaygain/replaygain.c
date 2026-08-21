#include "replaygain.h"
#include <math.h>
#include <string.h>

// Approximate -20 dBFS target for normalization
#define TARGET_RMS 0.1f 
#define SMOOTHING 0.00005f // Slow interpolation to not distort bass frequencies

void rgain_init(RGainState *rg, uint32_t sample_rate, uint32_t channels) {
    rg->sample_rate = sample_rate ? sample_rate : 44100;
    rg->channels = channels ? channels : 2;
    rg->current_multiplier = 1.0f;
    rg->target_multiplier = 1.0f;
    memset(rg->energy_history, 0, sizeof(rg->energy_history));
    rg->history_idx = 0;
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
    
    // If Meta mode, but the song does not have metadata for ReplayGain, just change to Calc
    if (rg->mode == RGAIN_CALC || (rg->mode == RGAIN_META && !rg->has_meta)) {
        for (uint32_t i = 0; i < num_frames; i++) {
            // Calculate spatial energy of current frame
            float sum_squares = 0.0f;
            for (uint32_t c = 0; c < rg->channels; c++) {
                float sample = (float)pcm[i * rg->channels + c] / 2147483648.0f;
                sum_squares += sample * sample;
            }
            
            rg->energy_history[rg->history_idx] = sum_squares / rg->channels;
            rg->history_idx = (rg->history_idx + 1) % 4096;
            
            // Sparse sampling of the window to compute RMS
            float window_energy = 0.0f;
            for (int j = 0; j < 4096; j += 64) {
                window_energy += rg->energy_history[j];
            }
            float rms = sqrtf(window_energy / (4096.0f / 64.0f));
            
            if (rms > 0.001f) {
                float desired_mult = TARGET_RMS / rms;
                // Prevent extreme explosion of silent gaps
                if (desired_mult > 3.0f) desired_mult = 3.0f; 
                if (desired_mult < 0.25f) desired_mult = 0.25f;
                
                rg->target_multiplier += SMOOTHING * (desired_mult - rg->target_multiplier);
            }
            
            rg->current_multiplier += SMOOTHING * 10.0f * (rg->target_multiplier - rg->current_multiplier);
            
            for (uint32_t c = 0; c < rg->channels; c++) {
                long long val = (long long)((float)pcm[i * rg->channels + c] * rg->current_multiplier);
                if (val > 2147483647LL) val = 2147483647LL;
                else if (val < -2147483648LL) val = -2147483648LL;
                pcm[i * rg->channels + c] = (int32_t)val;
            }
        }
    }
}