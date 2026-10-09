#ifndef VIS_MATH_H
#define VIS_MATH_H

#include <complex.h>
#include <stdint.h>
#include "state.h"

void vis_math_init(void);
extern float hann_window[FFT_SIZE];
void compute_fft(float complex *X, int N);
void set_braille_pixel(uint8_t *grid, int draw_w, int draw_h, int px_x, int px_y);
void draw_braille_line(uint8_t *grid, int draw_w, int draw_h, int x0, int y0, int x1, int y1);

void set_braille_pixel_colored(uint8_t *grid, uint8_t *col_grid, int draw_w, int draw_h, int px_x, int px_y, uint8_t color);
void draw_braille_line_colored(uint8_t *grid, uint8_t *col_grid, int draw_w, int draw_h, int x0, int y0, int x1, int y1, uint8_t color);

#endif // VIS_MATH_H