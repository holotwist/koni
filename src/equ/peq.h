#ifndef PEQ_H
#define PEQ_H

#include <stdint.h>
#include <stdbool.h>

#define PEQ_MAX_BANDS 10
#define PEQ_MAX_CHANNELS 8
#define PEQ_MIN_GAIN_DB -24.0f
#define PEQ_MAX_GAIN_DB  24.0f
#define PEQ_MIN_Q        0.1f
#define PEQ_MAX_Q        20.0f
#define PEQ_MIN_FREQ     20.0f
#define PEQ_MAX_FREQ     20000.0f

typedef enum {
    PEQ_FILTER_PEAK = 0,
    PEQ_FILTER_LOW_SHELF,
    PEQ_FILTER_HIGH_SHELF,
    PEQ_FILTER_HIGH_PASS,
    PEQ_FILTER_LOW_PASS,
    PEQ_FILTER_COUNT
} PEQFilterType;

typedef struct {
    bool enabled;
    PEQFilterType type;
    float freq;
    float gain_db;
    float q;
} PEQBand;

void peq_init(void);
void peq_reset(void);

int  peq_get_band_count(void);
bool peq_get_band(int idx, PEQBand *out_band);
void peq_set_band(int idx, const PEQBand *band);

float peq_get_preamp(void);
void  peq_set_preamp(float preamp_db);
void  peq_adjust_preamp(float delta_db);

void peq_toggle_band_enabled(int idx);
void peq_cycle_band_type(int idx);
void peq_adjust_band_freq(int idx, float multiplier);
void peq_adjust_band_gain(int idx, float delta_db);
void peq_adjust_band_q(int idx, float delta_q);

typedef struct {
    const char *name;
    const char *tagline;
} PEQPresetDef;

void peq_load_harman_target(bool in_ear);
const char* peq_get_filter_name(PEQFilterType type);
float peq_calculate_gain_at(float freq_hz);
void  peq_calculate_curve(float *out_db, int count, float min_freq, float max_freq);

// Built-in tuned presets
int  peq_get_builtin_preset_count(void);
const PEQPresetDef* peq_get_builtin_preset(int idx);
void peq_apply_builtin_preset(int idx);

// External preset I/O (AutoEQ / Equalizer APO format)
bool peq_load_file(const char *filepath);
bool peq_save_file(const char *filepath);
int  peq_scan_user_presets(char out_names[][128], char out_paths[][1024], int max_count);

void peq_save_state(void *file_ptr);
void peq_load_state_key(const char *key, const char *val);

void peq_process_float(float *samples_interleaved, uint32_t num_frames, uint16_t num_channels, uint32_t sample_rate);

#endif // PEQ_H