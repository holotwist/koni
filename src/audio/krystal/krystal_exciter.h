#ifndef KRYSTAL_EXCITER_H
#define KRYSTAL_EXCITER_H

#include <stdint.h>
#include <stdbool.h>
#include "krystal_profiles.h"
#include "krystal_bass.h"

typedef struct {
    uint32_t sample_rate;
    float last_cutoff;

    // High-pass filters for excitation band and ultra-air shimmer band
    KrystalBiquad hp_air;
    KrystalBiquad hp_shimmer;

    float smoothed_drive;
    float smoothed_mix;
    float smoothed_shimmer;
    float env[KRYSTAL_MAX_CHANNELS];
} KrystalExciterState;

void  krystal_exciter_init(KrystalExciterState *state, uint32_t sample_rate);
void  krystal_exciter_reset(KrystalExciterState *state);
float krystal_exciter_process(KrystalExciterState *state, float *samples, uint32_t num_frames,
                              uint16_t num_channels, const KrystalExciterConfig *cfg, uint32_t sample_rate);

#endif // KRYSTAL_EXCITER_H