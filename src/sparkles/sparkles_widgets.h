#ifndef SPARKLES_WIDGETS_H
#define SPARKLES_WIDGETS_H

#include "sparkles_grid.h"

// Individual tile hooks
void tile_playlists_render(SparklesTile *tile, Rectangle bounds);
void tile_playlists_input(SparklesTile *tile, Rectangle bounds);

void tile_song_list_render(SparklesTile *tile, Rectangle bounds);
void tile_song_list_input(SparklesTile *tile, Rectangle bounds);

void tile_album_art_render(SparklesTile *tile, Rectangle bounds);
void tile_album_art_input(SparklesTile *tile, Rectangle bounds);

void tile_waveform_render(SparklesTile *tile, Rectangle bounds);
void tile_waveform_input(SparklesTile *tile, Rectangle bounds);

void tile_transport_render(SparklesTile *tile, Rectangle bounds);
void tile_transport_input(SparklesTile *tile, Rectangle bounds);

#include "views/sparkles_player_view.h"

// Search state queries & openers
bool tile_song_list_is_searching(void);
void tile_song_list_open_search(void);
void tile_song_list_close_search(void);
void tile_song_list_locate_playing(void);

bool tile_playlists_is_searching(void);
void tile_playlists_open_search(void);
void tile_playlists_close_search(void);
void tile_playlists_refresh(void);

#endif // SPARKLES_WIDGETS_H