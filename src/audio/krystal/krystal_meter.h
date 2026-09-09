#ifndef KRYSTAL_METER_H
#define KRYSTAL_METER_H

#include <stdint.h>
#include <stdbool.h>
#include "krystal_profiles.h"

typedef struct {
    uint32_t sample_rate;

    // ITU-R BS.1770 K-weighting filter stages 
    KrystalBiquad stage1_shelf;
    KrystalBiquad stage2_highpass;

    float mean_square_acc;
    float lufs_momentary;
    float peak_dbfs;
} KrystalMeterState;

void krystal_meter_init(KrystalMeterState *meter, uint32_t sample_rate);
void krystal_meter_reset(KrystalMeterState *meter);

void krystal_meter_process(KrystalMeterState *meter, const float *samples, uint32_t num_frames,
                           uint16_t channels, uint32_t sample_rate,
                           float *out_lufs, float *out_peak_dbfs);

#endif // KRYSTAL_METER_H