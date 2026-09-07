#ifndef EQUALIZER_H
#define EQUALIZER_H

#include <stdint.h>
#include <stdbool.h>

#define EQ_NUM_BANDS 10
#define EQ_MIN_GAIN_DB -12.0f
#define EQ_MAX_GAIN_DB  12.0f

typedef struct {
    const char *name;
    float gains[EQ_NUM_BANDS];
} EQPreset;

void eq_init(void);
bool eq_is_enabled(void);
void eq_set_enabled(bool enabled);
void eq_toggle_enabled(void);

float eq_get_band_gain(int band_idx);
void  eq_set_band_gain(int band_idx, float gain_db);
void  eq_adjust_band_gain(int band_idx, float delta_db);

int   eq_get_preset_count(void);
const char* eq_get_preset_name(int preset_idx);
int   eq_get_current_preset(void);
void  eq_apply_preset(int preset_idx);
void  eq_cycle_preset(void);
void  eq_reset_flat(void);

const float* eq_get_frequencies(void);
const char** eq_get_freq_labels(void);

/* State persistence */
void eq_save_state(void *file_ptr);
void eq_load_state_key(const char *key, const char *val);

/* Process interleaved 32-bit PCM in-place with 10 biquads and soft limiter */
void eq_process(int32_t *pcm_interleaved, uint32_t num_frames, uint16_t num_channels, uint32_t sample_rate);

#endif // EQUALIZER_H