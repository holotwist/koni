#ifndef SPARKLES_NAV_H
#define SPARKLES_NAV_H

#include "raylib.h"
#include <stdbool.h>

typedef enum {
    PAGE_PLAYLISTS = -2,
    PAGE_LIBRARY   = -1,
    PAGE_PLAYER    =  0
} SparklesPageX;

typedef enum {
    LAYER_SURFACE = 0,
    LAYER_EQ      = 1,
    LAYER_KRYSTAL = 2
} SparklesLayerY;

void sparkles_nav_init(void);
void sparkles_nav_update(float dt);

void sparkles_nav_step_x(int delta);
void sparkles_nav_step_y(int delta);
void sparkles_nav_set_x(int x);
void sparkles_nav_set_y(int y);

float sparkles_nav_get_current_x(void);
float sparkles_nav_get_current_y(void);
int   sparkles_nav_get_target_x(void);
int   sparkles_nav_get_target_y(void);

bool sparkles_nav_is_eq_open(void);
bool sparkles_nav_is_krystal_open(void);

void sparkles_nav_render_badge(float screen_w, float screen_h);

#endif // SPARKLES_NAV_H