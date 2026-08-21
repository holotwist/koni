#include "vis_math.h"
#include <math.h>
#include <stdlib.h>

float hann_window[FFT_SIZE];
static float complex precalc_twiddles[FFT_SIZE];
static int math_initialized = 0;

void vis_math_init(void) {
    if (math_initialized) return;
    for (int i = 0; i < FFT_SIZE; i++) {
        hann_window[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (FFT_SIZE - 1)));
    }
    int idx = 0;
    for (int len = 2; len <= FFT_SIZE; len <<= 1) {
        float angle = -2.0f * (float)M_PI / len;
        float complex wlen = cosf(angle) + I * sinf(angle);
        float complex w = 1.0f;
        for (int j = 0; j < len / 2; j++) {
            precalc_twiddles[idx++] = w;
            w *= wlen;
        }
    }
    math_initialized = 1;
}

void compute_fft(float complex *X, int N) {
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float complex temp = X[i];
            X[i] = X[j];
            X[j] = temp;
        }
    }
    int idx = 0;
    for (int len = 2; len <= N; len <<= 1) {
        int half_len = len / 2;
        for (int i = 0; i < N; i += len) {
            for (int j = 0; j < half_len; j++) {
                float complex w = precalc_twiddles[idx + j];
                float complex u = X[i + j];
                float complex v = X[i + j + half_len] * w;
                X[i + j] = u + v;
                X[i + j + half_len] = u - v;
            }
        }
        idx += half_len;
    }
}

void set_braille_pixel(uint8_t *grid, int draw_w, int draw_h, int px_x, int px_y) {
    if (px_x < 0 || px_x >= draw_w * 2 || px_y < 0 || px_y >= draw_h * 4) return;
    int cell_x = px_x / 2;
    int cell_y = px_y / 4;
    int dot_x = px_x % 2;
    int dot_y = px_y % 4;
    static const uint8_t dot_mask[4][2] = {{0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80}};
    grid[cell_y * draw_w + cell_x] |= dot_mask[dot_y][dot_x];
}

void draw_braille_line(uint8_t *grid, int draw_w, int draw_h, int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    for (;;) {
        set_braille_pixel(grid, draw_w, draw_h, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}