#ifndef KRYSTAL_BASS_H
#define KRYSTAL_BASS_H

#include <stdint.h>
#include <stdbool.h>
#include "krystal_profiles.h"

#define BASS_DELAY_BUF_SIZE 1024

typedef struct {
    uint32_t sample_rate;
    float last_cutoff;
    float last_rumble;

    // 4th-order Linkwitz-Riley low-pass for sub extraction 
    KrystalBiquad lp_sub1;
    KrystalBiquad lp_sub2;

    // 4th-order Linkwitz-Riley high-pass for mid/high crossover complement 
    KrystalBiquad hp_mid1;
    KrystalBiquad hp_mid2;

    // Subsonic rumble filter 
    KrystalBiquad hp_sub;

    // Critically damped harmonic band-shaping filters 
    KrystalBiquad hp_harm;
    KrystalBiquad lp_harm;

    // 2nd-order Butterworth sub-octave smoothing filter 
    KrystalBiquad lp_sub_oct;

    // Dynamics & harmonic tracking state 
    float env[KRYSTAL_MAX_CHANNELS];
    float dc_state[KRYSTAL_MAX_CHANNELS];

    // Parameter smoothers 
    float smoothed_mix;
    float smoothed_intensity;
    float smoothed_sub_weight;
    float smoothed_tone;
    float smoothed_sub_octave;
    float smoothed_phase_deg;
} KrystalBassState;

void krystal_bass_init(KrystalBassState *state, uint32_t sample_rate);
void krystal_bass_reset(KrystalBassState *state);

float krystal_bass_process(KrystalBassState *state, float *samples, uint32_t num_frames,
                           uint16_t num_channels, const KrystalBassConfig *cfg, uint32_t sample_rate,
                           float *out_sub_energy, float *out_octave_energy);

#endif // KRYSTAL_BASS_H