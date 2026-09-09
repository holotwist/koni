#ifndef KRYSTAL_TRANSIENT_H
#define KRYSTAL_TRANSIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "krystal_profiles.h"

typedef struct {
    uint32_t sample_rate;
    float env_fast[KRYSTAL_MAX_CHANNELS];
    float env_slow[KRYSTAL_MAX_CHANNELS];
    float gain_smoothed[KRYSTAL_MAX_CHANNELS];
    float smoothed_attack;
    float smoothed_sustain;
    float smoothed_mix;
} KrystalTransientState;

void krystal_transient_init(KrystalTransientState *state, uint32_t sample_rate);
void krystal_transient_reset(KrystalTransientState *state);

// Reconstructs flattened sample peaks using cubic Hermite interpolation 
void krystal_declip_process(float *samples, uint32_t num_frames, uint16_t channels,
                            float threshold);

// Applies differential envelope transient shaping 
float krystal_transient_process(KrystalTransientState *state, float *samples, uint32_t num_frames,
                                uint16_t num_channels, const KrystalTransientConfig *cfg,
                                uint32_t sample_rate);

#endif // KRYSTAL_TRANSIENT_H