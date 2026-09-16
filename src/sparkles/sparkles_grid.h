#ifndef SPARKLES_GRID_H
#define SPARKLES_GRID_H

#include "sparkles_theme.h"
#include <stdbool.h>

typedef enum {
    TILE_HERO_DISPLAY = 0, // No longer available
    TILE_TRACK_LIST,       // Queue / Library mini-view
    TILE_ALBUM_ART,        // Cover art & track meta
    TILE_WAVEFORM_SEEK,    // Live Waveform seek bar
    TILE_TRANSPORT,        // Play/Pause, Next, Previous
    TILE_PLAYLIST_TABLE,   // Bottom library table
    TILE_TYPE_COUNT
} SparklesTileType;

typedef struct SparklesTile {
    int id;
    SparklesTileType type;
    const char *title;

    // Grid Coordinates (Integer units: 0..GRID_COLS-1, 0..GRID_ROWS-1)
    int gx, gy, gw, gh;

    // Pixel rectangle for rendering & interaction
    Rectangle rect;

    bool is_dragging;
    bool is_resizing;
    Vector2 drag_offset;

    void (*render)(struct SparklesTile *tile, Rectangle bounds);
    void (*handle_input)(struct SparklesTile *tile, Rectangle bounds);
} SparklesTile;

typedef struct {
    SparklesTile tiles[16];
    int tile_count;
    bool edit_mode;         // When true, allows moving and resizing tiles
    int active_drag_id;
    int active_resize_id;
} SparklesGrid;

void sparkles_grid_init(SparklesGrid *grid);
void sparkles_grid_update(SparklesGrid *grid, float screen_w, float screen_h, bool allow_input);
void sparkles_grid_render(SparklesGrid *grid);

#endif // SPARKLES_GRID_H