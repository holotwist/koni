#include "ui_common.h"
#include "visualizers.h"
#include "vis_math.h"
#include "vis_renderer.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static float *display_buf = NULL;
static int last_draw_w = 0;

void draw_vis_oscilloscope(int y, int x, int draw_w, int draw_h) {
    uint8_t *grid = vis_renderer_begin(draw_w, draw_h);

    if (draw_w != last_draw_w) {
        if (display_buf) free(display_buf);
        display_buf = malloc(sizeof(float) * 8192);
        last_draw_w = draw_w;
    }

    int px_w = draw_w * 2; int px_h = draw_h * 4;
    uint32_t srate = atomic_load(&vis_srate);
    if (srate == 0) srate = 44100;

    uint32_t window_samples = (srate * 40u) / 1000u;
    int num_samples = px_w * 2; if (num_samples > 8192) num_samples = 8192;
    for (int i = 0; i < num_samples; i++) {
        uint32_t neg_off = window_samples - (uint32_t)((i * window_samples) / num_samples);
        if (neg_off > ui_cache.smooth_rpos) display_buf[i] = 0.0f; 
        else display_buf[i] = (vis_ring_l[(ui_cache.smooth_rpos - neg_off) & VIS_BUF_MASK] + vis_ring_r[(ui_cache.smooth_rpos - neg_off) & VIS_BUF_MASK]) * 0.5f;
    }
    
    float peak = 0.01f;
    for (int i = 0; i < num_samples; i++) { float v = fabsf(display_buf[i]); if (v > peak) peak = v; }
    
    float scale = (1.0f / peak) * ((float)px_h / 2.2f);
    int center_y_px = px_h / 2;
    
    for (int i = 0; i < num_samples - 1; i++) {
        int x0 = (i * px_w) / num_samples; int x1 = ((i + 1) * px_w) / num_samples;
        int y0 = center_y_px - (int)(display_buf[i] * scale);
        int y1 = center_y_px - (int)(display_buf[i+1] * scale);
        draw_braille_line(grid, draw_w, draw_h, x0, y0, x1, y1);
    }
    
    vis_renderer_end(y, x, draw_w, draw_h, VIS_COLOR_COLUMN);
}