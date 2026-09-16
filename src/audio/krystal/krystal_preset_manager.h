#ifndef KRYSTAL_PRESET_MANAGER_H
#define KRYSTAL_PRESET_MANAGER_H

#include <stdbool.h>
#include "krystal_profiles.h"

#define KRYSTAL_MAX_CUSTOM_PRESETS 64
#define KRYSTAL_PRESET_NAME_MAX 64

void krystal_presets_init(void);
void krystal_presets_shutdown(void);

int krystal_presets_get_count(void);
const char *krystal_presets_get_name(int idx);
bool krystal_presets_get_config(int idx, KrystalConfig *out_cfg);

int krystal_presets_find(const char *name);
bool krystal_presets_save(const char *name, const KrystalConfig *cfg);
bool krystal_presets_delete(const char *name);
bool krystal_presets_rename(const char *old_name, const char *new_name);

#endif // KRYSTAL_PRESET_MANAGER_H