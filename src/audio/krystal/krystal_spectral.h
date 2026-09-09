#ifndef KRYSTAL_SPECTRAL_H
#define KRYSTAL_SPECTRAL_H

#include <stdint.h>
#include <stdbool.h>
#include "krystal_profiles.h"

typedef struct {
    uint32_t sample_rate;

    // Tilt filter pair pivoted at 1 kHz 
    KrystalBiquad tilt_low;
    KrystalBiquad tilt_high;
    float current_tilt_db;

    // Dynamic de-harsh band (~3.2 kHz) 
    KrystalBiquad sc_harsh;
    KrystalBiquad dyn_harsh;
    float env_harsh[KRYSTAL_MAX_CHANNELS];
    float current_harsh_cut_db;

    // Dynamic de-boom band (~160 Hz) 
    KrystalBiquad sc_boom;
    KrystalBiquad dyn_boom;
    float env_boom[KRYSTAL_MAX_CHANNELS];
    float current_boom_cut_db;

    float last_harsh_fc;
    float last_boom_fc;
} KrystalSpectralState;

void krystal_spectral_init(KrystalSpectralState *state, uint32_t sample_rate);
void krystal_spectral_reset(KrystalSpectralState *state);

void krystal_spectral_process(KrystalSpectralState *state, float *samples, uint32_t num_frames,
                              uint16_t num_channels, const KrystalSpectralConfig *cfg,
                              uint32_t sample_rate, float *out_harsh_cut_db, float *out_boom_cut_db);

#endif // KRYSTAL_SPECTRAL_H