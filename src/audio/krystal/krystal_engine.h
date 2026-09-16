#ifndef KRYSTAL_ENGINE_H
#define KRYSTAL_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "krystal_profiles.h"

void krystal_init(uint32_t sample_rate);
void krystal_reset(void);
void krystal_shutdown(void);

bool krystal_is_enabled(void);
void krystal_set_enabled(bool enabled);
void krystal_toggle_enabled(void);

void krystal_get_config(KrystalConfig *out_cfg);
void krystal_set_config(const KrystalConfig *cfg);
void krystal_apply_profile(int profile_idx);
void krystal_cycle_profile(void);
const char* krystal_get_active_preset_name(void);
void krystal_set_active_preset_name(const char *name);

void krystal_get_telemetry(KrystalTelemetry *out_telem);

void krystal_process(float *samples_interleaved, uint32_t num_frames, uint16_t num_channels, uint32_t sample_rate, int volume_percent);

void krystal_save_state(void *file_ptr);
void krystal_load_state_key(const char *key, const char *val);

#endif // KRYSTAL_ENGINE_H