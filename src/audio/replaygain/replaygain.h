#ifndef REPLAYGAIN_H
#define REPLAYGAIN_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    RGAIN_OFF = 0,
    RGAIN_META = 1,
    RGAIN_CALC = 2
} RGainMode;

typedef struct {
    RGainMode mode;
    float meta_gain_db;
    bool has_meta;

    // Static / Target tracking multipliers
    float current_multiplier;
    float target_multiplier;

    // Dual-stage loudness integrator
    float integrated_energy;
    float short_term_energy;
    float peak_energy;
    uint32_t frames_seen;
    uint32_t sample_rate;
    uint32_t channels;

    // Pre-calculated filter coefficients
    float alpha_fast;
    float alpha_slow;
    float alpha_slew;
} RGainState;

void rgain_init(RGainState *rg, uint32_t sample_rate, uint32_t channels);
void rgain_set_meta(RGainState *rg, bool has_meta, float track_gain_db);
void rgain_set_mode(RGainState *rg, RGainMode mode);

void rgain_process_float(RGainState *rg, float *samples, uint32_t num_frames);
void rgain_process(RGainState *rg, int32_t *pcm, uint32_t num_frames);

#endif // REPLAYGAIN_H