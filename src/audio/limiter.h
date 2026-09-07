#ifndef LIMITER_H
#define LIMITER_H

#include <stdint.h>
#include <stdbool.h>

#define LIMITER_MAX_CHANNELS 8
#define LIMITER_LOOKAHEAD_FRAMES 96

typedef struct {
    float delay_buf[LIMITER_LOOKAHEAD_FRAMES * LIMITER_MAX_CHANNELS];
    float gain_buf[LIMITER_LOOKAHEAD_FRAMES];
    uint32_t buf_idx;
    float current_gain;
    uint32_t sample_rate;
    float alpha_attack;
    float alpha_release;
    float ceiling;
} LookaheadLimiter;

void limiter_init(LookaheadLimiter *limiter, uint32_t sample_rate);
void limiter_reset(LookaheadLimiter *limiter);

/* Applies lookahead true-peak limiting in-place across multichannel buffers */
void limiter_process(LookaheadLimiter *limiter, float *samples_interleaved, uint32_t num_frames, uint16_t num_channels, uint32_t sample_rate);

#endif // LIMITER_H