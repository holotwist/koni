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
    
    // Automatic Gain Control (AGC)
    float current_multiplier;
    float target_multiplier;
    float energy_history[4096];
    int history_idx;
    uint32_t sample_rate;
    uint32_t channels;
} RGainState;

void rgain_init(RGainState *rg, uint32_t sample_rate, uint32_t channels);
void rgain_set_meta(RGainState *rg, bool has_meta, float track_gain_db);
void rgain_set_mode(RGainState *rg, RGainMode mode);

// 32-bit PCM
void rgain_process(RGainState *rg, int32_t *pcm, uint32_t num_frames);

#endif // REPLAYGAIN_H