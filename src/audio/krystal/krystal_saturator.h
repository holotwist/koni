#ifndef KRYSTAL_SATURATOR_H
#define KRYSTAL_SATURATOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "krystal_profiles.h"
#include "krystal_oversample.h"

typedef struct {
    uint32_t sample_rate;
    KrystalOversampler oversampler;
    float dc_state[KRYSTAL_MAX_CHANNELS];
    float hysteresis[KRYSTAL_MAX_CHANNELS];
    float tone_state[KRYSTAL_MAX_CHANNELS];
    float smoothed_drive;
    float smoothed_bias;
    float smoothed_tone;
    float smoothed_mix;
    float *buf_2x;
    size_t buf_2x_cap;
} KrystalSaturatorState;

void krystal_saturator_init(KrystalSaturatorState *state, uint32_t sample_rate);
void krystal_saturator_reset(KrystalSaturatorState *state);

float krystal_saturator_process(KrystalSaturatorState *state, float *samples, uint32_t num_frames,
                               uint16_t num_channels, const KrystalSatConfig *cfg, uint32_t sample_rate);

#endif // KRYSTAL_SATURATOR_H