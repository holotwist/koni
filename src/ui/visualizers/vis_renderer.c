#define _XOPEN_SOURCE_EXTENDED 1
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "vis_renderer.h"
#include "ui_common.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wchar.h>
#include <ncurses.h>

static uint8_t *grid = NULL;
static uint16_t *last_grid = NULL;
static float *radial_dist_lut = NULL;
static int last_draw_w = 0, last_draw_h = 0;
static const int radial_colors[6] = {6, 7, 8, 9, 5, 10};

uint8_t* vis_renderer_begin(int draw_w, int draw_h) {
    if (draw_w != last_draw_w || draw_h != last_draw_h) {
        if (grid) free(grid);
        if (last_grid) free(last_grid);
        if (radial_dist_lut) free(radial_dist_lut);
        
        grid = calloc((size_t)(draw_w * draw_h), sizeof(uint8_t));
        // Upgrade to uint16_t to allow a dirty state
        last_grid = malloc((size_t)(draw_w * draw_h) * sizeof(uint16_t));
        radial_dist_lut = malloc((size_t)(draw_w * draw_h) * sizeof(float));
        
        // 0xFF on a uint16_t array fills it with 0xFFFF
        memset(last_grid, 0xFF, (size_t)(draw_w * draw_h) * sizeof(uint16_t));
        
        // Precompute distance LUT for radial visualizers
        for (int gy = 0; gy < draw_h; gy++) {
            for (int gx = 0; gx < draw_w; gx++) {
                float dx = (float)gx - (draw_w / 2.0f);
                float dy = ((float)gy - (draw_h / 2.0f)) * 2.0f;
                radial_dist_lut[gy * draw_w + gx] = sqrtf(dx * dx + dy * dy);
            }
        }
        
        last_draw_w = draw_w; last_draw_h = draw_h;
    } else {
        memset(grid, 0, (size_t)(draw_w * draw_h));
    }

    if (vis_needs_full_redraw && last_grid) {
        memset(last_grid, 0xFF, (size_t)(draw_w * draw_h) * sizeof(uint16_t));
        vis_needs_full_redraw = false;
    }

    return grid;
}

void vis_renderer_end(int y, int x, int draw_w, int draw_h, VisColorStrategy color_strat) {
    float max_radius = (float)draw_h * 0.9f;
    if (max_radius < 1.0f) max_radius = 1.0f;

    for (int gy = 0; gy < draw_h; gy++) {
        int row_offset = gy * draw_w;

        for (int gx = 0; gx < draw_w; gx++) {
            int idx = row_offset + gx;
            uint8_t v = grid[idx];

            if (v != last_grid[idx]) {
                move(y + gy, x + gx);

                if (v == 0) {
                    addch(' ');
                } else {
                    int color_idx = 6;
                    int attr = A_NORMAL;

                    if (color_strat == VIS_COLOR_COLUMN) {
                        color_idx = 6 + (gx * 5) / draw_w;
                        if (color_idx > 10) color_idx = 10;
                        if (color_idx < 6) color_idx = 6;
                    } else if (color_strat == VIS_COLOR_RADIAL) {
                        float dist = radial_dist_lut[idx];
                        
                        int c_idx = (int)((dist / max_radius) * 6.0f);
                        if (c_idx > 5) c_idx = 5;
                        if (c_idx < 0) c_idx = 0;
                        color_idx = radial_colors[c_idx];
                        attr = (dist > max_radius * 0.3f) ? A_BOLD : A_NORMAL;
                    }

                    wchar_t wch[2] = { (wchar_t)(0x2800 + v), L'\0' };
                    cchar_t cch;
                    setcchar(&cch, wch, attr, color_idx, NULL);
                    add_wch(&cch);
                }
                last_grid[idx] = v;
            }
        }
    }
}