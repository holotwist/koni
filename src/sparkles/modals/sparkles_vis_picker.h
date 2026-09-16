#ifndef SPARKLES_VIS_PICKER_H
#define SPARKLES_VIS_PICKER_H

#include "raylib.h"
#include <stdbool.h>

void sparkles_vis_picker_init(void);
void sparkles_vis_picker_open(void);
void sparkles_vis_picker_close(void);
void sparkles_vis_picker_toggle(void);
bool sparkles_vis_picker_is_open(void);
bool sparkles_vis_picker_is_visible(void);
float sparkles_vis_picker_get_anim_progress(void);

void sparkles_vis_picker_update(float screen_w, float screen_h);
bool sparkles_vis_picker_handle_input(float screen_w, float screen_h);
void sparkles_vis_picker_render(float screen_w, float screen_h);

#endif // SPARKLES_VIS_PICKER_H