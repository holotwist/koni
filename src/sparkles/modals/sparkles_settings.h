#ifndef SPARKLES_SETTINGS_H
#define SPARKLES_SETTINGS_H

#include "raylib.h"
#include <stdbool.h>

void sparkles_settings_init(void);
void sparkles_settings_open(void);
void sparkles_settings_close(void);
void sparkles_settings_toggle(void);
bool sparkles_settings_is_open(void);
bool sparkles_settings_is_visible(void);
float sparkles_settings_get_anim_progress(void);

void sparkles_settings_update(float screen_w, float screen_h);
bool sparkles_settings_handle_input(float screen_w, float screen_h);
void sparkles_settings_render(float screen_w, float screen_h);

#endif // SPARKLES_SETTINGS_H