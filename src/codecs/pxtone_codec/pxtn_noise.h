#ifndef PXTONE_NOISE_H
#define PXTONE_NOISE_H

#include "pxtn_types.h"

typedef struct {
    int type;
    float freq;
    float vol;
    float offset;
    bool b_rev;
} PxOsc;

typedef struct {
    bool enable;
    int env_num;
    PxPoint enves[16];
    int pan;
    PxOsc main_osc;
    PxOsc freq_osc;
    PxOsc vol_osc;
} PxNoiseUnit;

void pxtn_synth_noise(PxVoiceUnit *vu, PxNoiseUnit *units, int unit_num, int smp_num_44k, uint32_t target_sps);

#endif // PXTONE_NOISE_H