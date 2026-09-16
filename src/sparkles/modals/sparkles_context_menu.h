#ifndef SPARKLES_CONTEXT_MENU_H
#define SPARKLES_CONTEXT_MENU_H

#include "raylib.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char path[1024];
    char title[256];
    char artist[256];
    char album[256];
    uint32_t duration_sec;
    bool in_playlist;
    char playlist_name[128];
    int playlist_track_idx;
} SparklesTrackItem;

void sparkles_context_menu_init(void);
void sparkles_context_menu_open(Vector2 mouse_pos, const SparklesTrackItem *track);
void sparkles_context_menu_open_playlist(Vector2 mouse_pos, const char *playlist_name);
void sparkles_context_menu_close(void);
bool sparkles_context_menu_is_open(void);

bool sparkles_context_menu_update(void);
void sparkles_context_menu_render(float screen_w, float screen_h);

#endif // SPARKLES_CONTEXT_MENU_H