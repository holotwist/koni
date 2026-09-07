#define _DEFAULT_SOURCE
#include "limiter.h"
#include <math.h>
#include <string.h>

void limiter_init(LookaheadLimiter *limiter, uint32_t sample_rate) {
    if (!limiter) return;
    memset(limiter, 0, sizeof(LookaheadLimiter));

    if (sample_rate == 0) sample_rate = 44100;
    limiter->sample_rate = sample_rate;

    // Standard broadcast true-peak ceiling, -0.2 dBFS (~0.9772f)
    limiter->ceiling = 0.9772f;
    limiter->current_gain = 1.0f;

    for (uint32_t i = 0; i < LIMITER_LOOKAHEAD_FRAMES; i++) {
        limiter->gain_buf[i] = 1.0f;
    }

    // Attack time matched to the lookahead delay (~2.1 ms at 44.1 kHz)
    limiter->alpha_attack = 1.0f - expf(-3.0f / (float)LIMITER_LOOKAHEAD_FRAMES);

    // 50ms exponential release curve
    const float release_sec = 0.050f;
    limiter->alpha_release = 1.0f - expf(-1.0f / ((float)sample_rate * release_sec));
}

void limiter_reset(LookaheadLimiter *limiter) {
    if (!limiter) return;
    memset(limiter->delay_buf, 0, sizeof(limiter->delay_buf));
    for (uint32_t i = 0; i < LIMITER_LOOKAHEAD_FRAMES; i++) {
        limiter->gain_buf[i] = 1.0f;
    }
    limiter->buf_idx = 0;
    limiter->current_gain = 1.0f;
}

void limiter_process(LookaheadLimiter *limiter, float *samples_interleaved, uint32_t num_frames, uint16_t num_channels, uint32_t sample_rate) {
    if (!limiter || !samples_interleaved || num_frames == 0 || num_channels == 0) return;

    if (sample_rate != limiter->sample_rate) {
        limiter_init(limiter, sample_rate);
    }

    uint16_t channels = (num_channels <= LIMITER_MAX_CHANNELS) ? num_channels : LIMITER_MAX_CHANNELS;
    float ceiling = limiter->ceiling;
    float alpha_attack = limiter->alpha_attack;
    float alpha_release = limiter->alpha_release;

    for (uint32_t f = 0; f < num_frames; f++) {
        uint32_t base = f * num_channels;

        // Linked multi-channel peak detection
        float peak = 0.0f;
        for (uint16_t c = 0; c < channels; c++) {
            float val = fabsf(samples_interleaved[base + c]);
            if (val > peak) peak = val;
        }

        // Compute instantaneous required target gain
        float target_gain = (peak > ceiling) ? (ceiling / peak) : 1.0f;

        // Store incoming frame and target gain into lookahead circular buffers
        uint32_t write_idx = limiter->buf_idx;
        for (uint16_t c = 0; c < channels; c++) {
            limiter->delay_buf[write_idx * channels + c] = samples_interleaved[base + c];
        }
        limiter->gain_buf[write_idx] = target_gain;

        // Inspect minimum gain needed across lookahead window
        float min_target = 1.0f;
        for (uint32_t i = 0; i < LIMITER_LOOKAHEAD_FRAMES; i++) {
            if (limiter->gain_buf[i] < min_target) {
                min_target = limiter->gain_buf[i];
            }
        }

        // Smoothly slew gain envelope
        if (min_target < limiter->current_gain) {
            // Smooth attack before the peak arrives
            limiter->current_gain += alpha_attack * (min_target - limiter->current_gain);
            if (limiter->current_gain < min_target) {
                limiter->current_gain = min_target;
            }
        } else {
            // Exponential release recovery
            limiter->current_gain += alpha_release * (1.0f - limiter->current_gain);
            if (limiter->current_gain > 1.0f) {
                limiter->current_gain = 1.0f;
            }
        }

        // Anti-denormal protection
        if (fabsf(limiter->current_gain) < 1.0e-15f) {
            limiter->current_gain = 0.0f;
        }

        // Fetch delayed frame that was stored L samples ago
        uint32_t read_idx = (write_idx + 1) % LIMITER_LOOKAHEAD_FRAMES;
        for (uint16_t c = 0; c < channels; c++) {
            float delayed_sample = limiter->delay_buf[read_idx * channels + c];
            float out_sample = delayed_sample * limiter->current_gain;

            // Safety transparent soft-clip (only catches sub-sample numerical edges)
            if (out_sample > ceiling) {
                float excess = out_sample - ceiling;
                out_sample = ceiling + (1.0f - ceiling) * tanhf(excess / (1.0f - ceiling));
            } else if (out_sample < -ceiling) {
                float excess = -out_sample - ceiling;
                out_sample = -(ceiling + (1.0f - ceiling) * tanhf(excess / (1.0f - ceiling)));
            }

            samples_interleaved[base + c] = out_sample;
        }

        limiter->buf_idx = read_idx;
    }
}