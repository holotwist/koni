#ifndef VIS_RENDERER_H
#define VIS_RENDERER_H

#include <stdint.h>

typedef enum {
    VIS_COLOR_COLUMN,
    VIS_COLOR_RADIAL
} VisColorStrategy;

// Initializes or resizes the grids if needed, clears the current grid
// Returns the current front grid for drawing
uint8_t* vis_renderer_begin(int draw_w, int draw_h);

// Renders the grid to the screen using the specified color strategy
void vis_renderer_end(int y, int x, int draw_w, int draw_h, VisColorStrategy color_strat);

#endif // VIS_RENDERER_H