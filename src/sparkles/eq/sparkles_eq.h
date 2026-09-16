#ifndef SPARKLES_EQ_H
#define SPARKLES_EQ_H

#include "raylib.h"
#include <stdbool.h>

void sparkles_eq_init(void);
void sparkles_eq_toggle(void);
bool sparkles_eq_is_open(void);

// Returns true if transition or screen is actively visible
bool sparkles_eq_is_visible(void);
float sparkles_eq_get_anim_progress(void);

void sparkles_eq_update(float screen_w, float screen_h);
void sparkles_eq_render(float screen_w, float screen_h);

#endif // SPARKLES_EQ_H