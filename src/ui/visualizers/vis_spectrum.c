#include "ui_common.h"
#include "visualizers.h"
#include "vis_math.h"
#include "vis_renderer.h"
#include <math.h>
#include <string.h>

typedef struct {
    int b1;
    int b2;
} BinRange;

static float smooth_bars[4096] = {0};
static BinRange bin_ranges[4096] = {0};
static int last_draw_w = 0;

void draw_vis_spectrum(int y, int x, int draw_w, int draw_h) {
    uint8_t *grid = vis_renderer_begin(draw_w, draw_h);
    int px_w = draw_w * 2; 
    int px_h = draw_h * 4;
    if (px_w > 4096) px_w = 4096;

    // Only compute logarithmic bins when the window width changes
    if (draw_w != last_draw_w) {
        memset(smooth_bars, 0, sizeof(smooth_bars));
        last_draw_w = draw_w;

        float min_f = log10f(1.0f); 
        float max_f = log10f((float)(FFT_SIZE / 2));
        for (int px = 0; px < px_w; px++) {
            float low_bin = powf(10.0f, min_f + (max_f - min_f) * ((float)px / (float)px_w));
            float high_bin = powf(10.0f, min_f + (max_f - min_f) * ((float)(px + 1) / (float)px_w));
            int b1 = (int)low_bin;
            int b2 = (int)high_bin;
            if (b2 <= b1) b2 = b1 + 1;
            if (b1 > (FFT_SIZE / 2)) b1 = (FFT_SIZE / 2);
            if (b2 > (FFT_SIZE / 2)) b2 = (FFT_SIZE / 2);
            bin_ranges[px].b1 = b1;
            bin_ranges[px].b2 = b2;
        }
    }

    float complex X[FFT_SIZE];
    for (int i = 0; i < FFT_SIZE; i++) {
        uint32_t idx = (ui_cache.smooth_rpos + VIS_BUF_SIZE - FFT_SIZE + i) & VIS_BUF_MASK;
        float val = (vis_ring_l[idx] + vis_ring_r[idx]) * 0.5f * hann_window[i];
        if (fabsf(val) < 1e-15f) val = 0.0f;
        X[i] = val;
    }
    
    compute_fft(X, FFT_SIZE);
    
    for (int px = 0; px < px_w; px++) {
        int b1 = bin_ranges[px].b1;
        int b2 = bin_ranges[px].b2;
        float peak_sq = 0.0f;
        
        // Compare squared magnitudes
        for (int b = b1; b < b2; b++) { 
            float r = crealf(X[b]);
            float im = cimagf(X[b]);
            float mag_sq = r * r + im * im;
            if (mag_sq > peak_sq) peak_sq = mag_sq; 
        }
        
        float peak = sqrtf(peak_sq);
        float norm_mag = peak / (FFT_SIZE / 4.0f); 
        float val = log10f(1.0f + norm_mag * 100.0f) / 2.0f; 
        if (val > 1.0f) val = 1.0f;
        
        PlayState st = (PlayState)atomic_load(&play_state_atomic);
        if (st == STATE_PLAYING) {
            if (val >= smooth_bars[px]) {
                smooth_bars[px] = val; 
            } else { 
                smooth_bars[px] -= 0.04f; 
                if (smooth_bars[px] < 0.0f) smooth_bars[px] = 0.0f; 
            }
        } else if (st == STATE_STOPPED) {
            smooth_bars[px] = 0.0f;
        }
        
        int bar_h = (int)(smooth_bars[px] * px_h); 
        if (bar_h <= 0) continue; // Skip silent/empty bars entirely
        if (bar_h > px_h) bar_h = px_h;

        int y1 = px_h - 1; 
        int y0 = px_h - bar_h; 
        if (y0 < 0) y0 = 0;
        
        // Fast-path vertical braille column, writes entire cells at once
        int cell_x = px / 2;
        int dot_x = px % 2;
        int top_cy = y0 / 4;
        int bot_cy = y1 / 4;
        static const uint8_t dot_mask[4][2] = {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};
        uint8_t full_mask = (dot_x == 0) ? 0x47 : 0xB8;

        if (top_cy == bot_cy) {
            uint8_t mask = 0;
            for (int py = y0; py <= y1; py++) mask |= dot_mask[py % 4][dot_x];
            grid[top_cy * draw_w + cell_x] |= mask;
        } else {
            uint8_t top_mask = 0;
            for (int dy = y0 % 4; dy < 4; dy++) top_mask |= dot_mask[dy][dot_x];
            grid[top_cy * draw_w + cell_x] |= top_mask;

            for (int cy = top_cy + 1; cy < bot_cy; cy++) {
                grid[cy * draw_w + cell_x] |= full_mask;
            }

            uint8_t bot_mask = 0;
            for (int dy = 0; dy <= (y1 % 4); dy++) bot_mask |= dot_mask[dy][dot_x];
            grid[bot_cy * draw_w + cell_x] |= bot_mask;
        }
    }
    
    vis_renderer_end(y, x, draw_w, draw_h, VIS_COLOR_COLUMN);
}