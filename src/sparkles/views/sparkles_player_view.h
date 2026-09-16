#ifndef SPARKLES_PLAYER_VIEW_H
#define SPARKLES_PLAYER_VIEW_H

#include "raylib.h"
#include <stdbool.h>

void sparkles_player_view_init(void);
void sparkles_player_view_toggle_lyrics(void);
void sparkles_player_view_toggle_help(void);
bool sparkles_player_view_is_help_visible(void);
void sparkles_player_view_render(float screen_w, float screen_h);
void sparkles_player_view_input(float screen_w, float screen_h);

// Tile adapter for the grid view
struct SparklesTile;
void sparkles_player_tile_render(struct SparklesTile *tile, Rectangle bounds);
void sparkles_player_tile_input(struct SparklesTile *tile, Rectangle bounds);

#endif // SPARKLES_PLAYER_VIEW_H