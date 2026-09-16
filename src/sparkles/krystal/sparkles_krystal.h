#ifndef SPARKLES_KRYSTAL_H
#define SPARKLES_KRYSTAL_H

#include "raylib.h"
#include <stdbool.h>

void sparkles_krystal_init(void);
void sparkles_krystal_toggle(void);
bool sparkles_krystal_is_open(void);
bool sparkles_krystal_is_visible(void);
float sparkles_krystal_get_anim_progress(void);

void sparkles_krystal_update(float screen_w, float screen_h);
void sparkles_krystal_render(float screen_w, float screen_h);

#endif // SPARKLES_KRYSTAL_H