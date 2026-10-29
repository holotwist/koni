#ifndef VISUALIZERS_H
#define VISUALIZERS_H

#include <stdint.h>
#include <stdbool.h>

void draw_vis_spectrum(int y, int x, int draw_w, int draw_h);
void draw_vis_oscilloscope(int y, int x, int draw_w, int draw_h);
bool vis_oscilloscope_is_true_mode(void);
void vis_oscilloscope_toggle_true_mode(void);
void vis_oscilloscope_step_zoom(int delta);
uint32_t vis_oscilloscope_get_timebase_ms(void);
bool vis_oscilloscope_handle_key(int ch);
void draw_vis_ellipse(int y, int x, int draw_w, int draw_h);
void draw_vis_lissajous(int y, int x, int draw_w, int draw_h);
void draw_vis_dancer(int y, int x, int draw_w, int draw_h);
void draw_vis_galvanometer(int y, int x, int draw_w, int draw_h);
void draw_vis_reels(int y, int x, int draw_w, int draw_h);

const char* get_vis_mode_name(int mode);

#endif // VISUALIZERS_H