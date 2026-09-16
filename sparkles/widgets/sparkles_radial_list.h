#ifndef SPARKLES_RADIAL_LIST_H
#define SPARKLES_RADIAL_LIST_H

#include "raylib.h"
#include <stdbool.h>

void sparkles_radial_list_init(void);
void sparkles_radial_list_open(void);
void sparkles_radial_list_close(void);
void sparkles_radial_list_toggle(void);
bool sparkles_radial_list_is_open(void);
bool sparkles_radial_list_is_visible(void);
float sparkles_radial_list_get_anim_progress(void);

void sparkles_radial_list_update(float screen_w, float screen_h);
void sparkles_radial_list_render(float screen_w, float screen_h);
bool sparkles_radial_list_handle_input(float screen_w, float screen_h);

#endif // SPARKLES_RADIAL_LIST_H