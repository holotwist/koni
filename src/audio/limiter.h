#ifndef LIMITER_H
#define LIMITER_H

#include <stdint.h>
#include <stdbool.h>

#define LIMITER_MAX_CHANNELS 8
#define LIMITER_LOOKAHEAD_FRAMES 96
#define LIMITER_POLYPHASE_TAPS 8

typedef struct {
    float delay_buf[LIMITER_LOOKAHEAD_FRAMES * LIMITER_MAX_CHANNELS];
    float gain_buf[LIMITER_LOOKAHEAD_FRAMES];
    uint32_t buf_idx;
    float current_gain;
    uint32_t sample_rate;
    float alpha_attack;
    float alpha_release;
    float ceiling;

    // ITU-R BS.1770 4x Polyphase True Peak Detector
    float peak_history[LIMITER_MAX_CHANNELS][LIMITER_POLYPHASE_TAPS];
    uint32_t hist_idx;
    float fir_phases[4][LIMITER_POLYPHASE_TAPS];
} LookaheadLimiter;

void limiter_init(LookaheadLimiter *limiter, uint32_t sample_rate);
void limiter_reset(LookaheadLimiter *limiter);

// Applies lookahead true-peak limiting in-place across multichannel buffers 
void limiter_process(LookaheadLimiter *limiter, float *samples_interleaved, uint32_t num_frames, uint16_t num_channels, uint32_t sample_rate);

#endif // LIMITER_H