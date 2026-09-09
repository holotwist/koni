#ifndef KRYSTAL_LOUDNESS_H
#define KRYSTAL_LOUDNESS_H

#include <stdint.h>
#include <stdbool.h>
#include "krystal_profiles.h"

typedef struct {
    uint32_t sample_rate;
    KrystalBiquad low_shelf;
    KrystalBiquad mid_dip;
    KrystalBiquad high_shelf;
    float current_bass_gain;
    float current_mid_gain;
    float current_treb_gain;
    float smoothed_volume;
} KrystalLoudnessState;

void krystal_loudness_init(KrystalLoudnessState *state, uint32_t sample_rate);
void krystal_loudness_reset(KrystalLoudnessState *state);
void krystal_loudness_process(KrystalLoudnessState *state, float *samples, uint32_t num_frames,
                              uint16_t num_channels, const KrystalLoudnessConfig *cfg,
                              uint32_t sample_rate, int volume_percent,
                              float *out_bass_db, float *out_treb_db);

#endif // KRYSTAL_LOUDNESS_H