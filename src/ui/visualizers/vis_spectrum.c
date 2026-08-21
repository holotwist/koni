#include "ui_common.h"
#include "visualizers.h"
#include "vis_math.h"
#include "vis_renderer.h"
#include <math.h>
#include <string.h>

static float smooth_bars[4096] = {0};
static int last_draw_w = 0;

void draw_vis_spectrum(int y, int x, int draw_w, int draw_h) {
    uint8_t *grid = vis_renderer_begin(draw_w, draw_h);
    
    if (draw_w != last_draw_w) {
        memset(smooth_bars, 0, sizeof(smooth_bars));
        last_draw_w = draw_w;
    }

    int px_w = draw_w * 2; 
    int px_h = draw_h * 4;

    float complex X[FFT_SIZE];
    for (int i = 0; i < FFT_SIZE; i++) {
        uint32_t idx = (ui_cache.smooth_rpos + VIS_BUF_SIZE - FFT_SIZE + i) & VIS_BUF_MASK;
        float val = (vis_ring_l[idx] + vis_ring_r[idx]) * 0.5f * hann_window[i];
        if (fabsf(val) < 1e-15f) val = 0.0f;
        X[i] = val;
    }
    
    compute_fft(X, FFT_SIZE);
    float min_f = log10f(1.0f); 
    float max_f = log10f((float)(FFT_SIZE / 2));
    
    for (int px = 0; px < px_w; px++) {
        float low_bin = powf(10.0f, min_f + (max_f - min_f) * ((float)px / px_w));
        float high_bin = powf(10.0f, min_f + (max_f - min_f) * ((float)(px + 1) / px_w));
        int b1 = (int)low_bin; int b2 = (int)high_bin; if (b2 <= b1) b2 = b1 + 1;
        float peak = 0.0f;
        
        for (int b = b1; b < b2 && b < FFT_SIZE / 2; b++) { 
            float mag = cabsf(X[b]); 
            if (mag > peak) peak = mag; 
        }
        
        float norm_mag = peak / (FFT_SIZE / 4.0f); 
        float val = log10f(1.0f + norm_mag * 100.0f) / 2.0f; 
        if (val > 1.0f) val = 1.0f;
        
        if (val >= smooth_bars[px]) smooth_bars[px] = val; 
        else { 
            smooth_bars[px] -= 0.04f; 
            if (smooth_bars[px] < 0.0f) smooth_bars[px] = 0.0f; 
        }
        
        int bar_h = (int)(smooth_bars[px] * px_h); 
        if (bar_h > px_h) bar_h = px_h;
        int y1 = px_h - 1; 
        int y0 = px_h - bar_h; 
        if (y0 < 0) y0 = 0;
        
        draw_braille_line(grid, draw_w, draw_h, px, y0, px, y1);
    }
    
    vis_renderer_end(y, x, draw_w, draw_h, VIS_COLOR_COLUMN);
}