#include "ui_common.h"
#include "visualizers.h"
#include "vis_math.h"
#include "vis_renderer.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

void draw_vis_lissajous(int y, int x, int draw_w, int draw_h) {
    uint8_t *grid = vis_renderer_begin(draw_w, draw_h);

    int px_w = draw_w * 2; 
    int px_h = draw_h * 4;
    uint32_t srate = atomic_load(&vis_srate);
    if (srate == 0) srate = 44100;

    uint32_t window_samples = (srate * 50u) / 1000u;
    if (window_samples > VIS_BUF_SIZE) window_samples = VIS_BUF_SIZE;
    
    int center_px_x = px_w / 2;
    int center_px_y = px_h / 2;
    
    float scale_x = (float)center_px_x * 0.95f;
    float scale_y = (float)center_px_y * 0.95f;
    int prev_x = -1, prev_y = -1;

    for (uint32_t i = 0; i < window_samples; i += 2) {
        uint32_t idx = (ui_cache.smooth_rpos - i) & VIS_BUF_MASK;
        float l = vis_ring_l[idx];
        float r = vis_ring_r[idx];
        
        float mid = (l + r) * 0.7071f * 1.3f;
        float side = (l - r) * 0.7071f * 1.3f;

        int px = center_px_x + (int)(side * scale_x);
        int py = center_px_y - (int)(mid * scale_y);
        
        if (prev_x != -1) {
            draw_braille_line(grid, draw_w, draw_h, prev_x, prev_y, px, py);
        } else {
            set_braille_pixel(grid, draw_w, draw_h, px, py);
        }
        prev_x = px; 
        prev_y = py;
    }
    
    vis_renderer_end(y, x, draw_w, draw_h, VIS_COLOR_RADIAL);
}