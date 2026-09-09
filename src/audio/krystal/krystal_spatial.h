#ifndef KRYSTAL_SPATIAL_H
#define KRYSTAL_SPATIAL_H

#include <stdint.h>
#include <stdbool.h>
#include "krystal_profiles.h"

#define SPATIAL_RING_SIZE 4096

typedef struct {
    uint32_t sample_rate;
    float last_mono_cut;

    KrystalBiquad hp_side1;
    KrystalBiquad hp_side2;

    // Mid Channel HRTF Biquads 
    KrystalBiquad m_shadow_l;
    KrystalBiquad m_shadow_r;
    KrystalBiquad m_rear_l;
    KrystalBiquad m_rear_r;

    // Side Channel HRTF Biquads 
    KrystalBiquad s_shadow_l;
    KrystalBiquad s_shadow_r;
    KrystalBiquad s_rear_l;
    KrystalBiquad s_rear_r;

    // Elevation Pinna Notches 
    KrystalBiquad pinna_l;
    KrystalBiquad pinna_r;

    // Orthogonal Delay Rings 
    float delay_ring_m[SPATIAL_RING_SIZE];
    float delay_ring_s[SPATIAL_RING_SIZE];
    
    // Room Reflection Rings 
    float refl_ring_l[SPATIAL_RING_SIZE];
    float refl_ring_r[SPATIAL_RING_SIZE];
    uint32_t write_idx;

    float air_damp_l;
    float air_damp_r;

    float smoothed_azimuth;
    float smoothed_elevation;
    float smoothed_distance;
    float smoothed_stage_angle;
    float smoothed_room_refl;
    float smoothed_center_mult;

    // Dynamic per-sample interpolation tracking for Doppler Effect 
    float cur_delay_m_l;
    float cur_delay_m_r;
    float cur_delay_s_l;
    float cur_delay_s_r;

    float phase_corr_filtered;
    float mid_energy_filtered;
    float side_energy_filtered;
} KrystalSpatialState;

void krystal_spatial_init(KrystalSpatialState *state, uint32_t sample_rate);
void krystal_spatial_reset(KrystalSpatialState *state);

void krystal_spatial_process(KrystalSpatialState *state, float *samples, uint32_t num_frames,
                             const KrystalSpatialConfig *cfg, uint32_t sample_rate,
                             float *out_mid_energy, float *out_side_energy,
                             float *out_phase_corr, bool *out_clamped);

#endif // KRYSTAL_SPATIAL_H