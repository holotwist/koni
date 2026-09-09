#ifndef KRYSTAL_OVERSAMPLE_H
#define KRYSTAL_OVERSAMPLE_H

#include <stdint.h>
#include <stdbool.h>
#include "krystal_profiles.h"

#define HB_TAPS 23
#define HB_HALF ((HB_TAPS - 1) / 2)

typedef struct {
    float up_history[KRYSTAL_MAX_CHANNELS][HB_HALF + 1];
    float down_history[KRYSTAL_MAX_CHANNELS][HB_TAPS];
    uint32_t up_idx;
    uint32_t down_idx;
} KrystalOversampler;

void krystal_oversampler_init(KrystalOversampler *os);
void krystal_oversampler_reset(KrystalOversampler *os);

// 2x upsampling. out_samples must hold (num_frames * 2 * num_channels) floats 
void krystal_oversample_up(KrystalOversampler *os, const float *in, float *out_2x,
                           uint32_t num_frames, uint16_t channels);

// 2x decimation. out must hold (num_frames * num_channels) floats 
void krystal_oversample_down(KrystalOversampler *os, const float *in_2x, float *out,
                             uint32_t num_frames, uint16_t channels);

#endif // KRYSTAL_OVERSAMPLE_H