#include "ui_common.h"
#include "visualizers/visualizers.h"
#include <string.h>
#include <stdlib.h>

const char* get_vis_mode_name(int mode) {
    if (mode == 0) return "Spectrum";
    if (mode == 1) return "Oscilloscope";
    if (mode == 2) return "Ellipse";
    if (mode == 3) return "Lissajous";
    return "Unknown";
}

void draw_vis_panel(int y, int x, int h, int w) {
    if (h < 4 || w < 4) return;
    
    // Pass NULL for the title so it doesn't collide with the tabs menu
    ui_draw_box(y, x, h, w, NULL, 1);

    // Tabs Menu
    mvprintw(y, x + 2, " ");
    attron(active_tab == 1 ? A_REVERSE : A_NORMAL); 
    printw("1:%s", get_vis_mode_name(current_vis_mode)); 
    attroff(active_tab == 1 ? A_REVERSE : A_NORMAL); printw(" ");
    
    if (ui_cache.meta.lyrics != NULL) {
        attron(active_tab == 2 ? A_REVERSE : A_NORMAL); printw("2:lyric"); attroff(active_tab == 2 ? A_REVERSE : A_NORMAL); printw(" ");
    }

    int draw_w = w - 4;
    int draw_h = h - 4;
    if (draw_w < 1 || draw_h < 1) return;

    if (active_tab == 1) {
        if (current_vis_mode == 0) draw_vis_spectrum(y + 2, x + 2, draw_w, draw_h);
        else if (current_vis_mode == 1) draw_vis_oscilloscope(y + 2, x + 2, draw_w, draw_h);
        else if (current_vis_mode == 2) draw_vis_ellipse(y + 2, x + 2, draw_w, draw_h);
        else if (current_vis_mode == 3) draw_vis_lissajous(y + 2, x + 2, draw_w, draw_h);
        
    } else if (active_tab == 2) {
        for (int gy = 0; gy < draw_h; gy++) mvhline(y + 2 + gy, x + 2, ' ', draw_w);
        if (ui_cache.meta.lyrics) {
            int ly = y + 2;
            char* lyrics_copy = strdup(ui_cache.meta.lyrics);
            char* line = strtok(lyrics_copy, "\n");
            while (line && ly < y + h - 1) {
                mvprintw(ly++, x + 4, "%.*s", draw_w - 2, line);
                line = strtok(NULL, "\n");
            }
            free(lyrics_copy);
        } else {
            mvprintw(y + 2, x + 4, "No Lyrics available");
        }
    }
}