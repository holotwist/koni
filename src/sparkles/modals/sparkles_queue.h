#ifndef SPARKLES_QUEUE_H
#define SPARKLES_QUEUE_H

#include "raylib.h"
#include <stdbool.h>

void sparkles_queue_init(void);
void sparkles_queue_toggle(void);
bool sparkles_queue_is_open(void);
bool sparkles_queue_is_visible(void);
float sparkles_queue_get_anim_progress(void);

void sparkles_queue_update(float screen_w, float screen_h);
void sparkles_queue_render(float screen_w, float screen_h);

#endif // SPARKLES_QUEUE_H