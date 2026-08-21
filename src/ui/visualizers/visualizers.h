#ifndef VISUALIZERS_H
#define VISUALIZERS_H

void draw_vis_spectrum(int y, int x, int draw_w, int draw_h);
void draw_vis_oscilloscope(int y, int x, int draw_w, int draw_h);
void draw_vis_ellipse(int y, int x, int draw_w, int draw_h);
void draw_vis_lissajous(int y, int x, int draw_w, int draw_h);

const char* get_vis_mode_name(int mode);

#endif // VISUALIZERS_H