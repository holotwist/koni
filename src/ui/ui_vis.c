#include "ui_common.h"
#include "config.h"
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
    
    attron(active_tab == 2 ? A_REVERSE : A_NORMAL); printw("2:lyric"); attroff(active_tab == 2 ? A_REVERSE : A_NORMAL); printw(" ");

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
        
        if (!app_config.online_lyrics_asked) {
            int py = y + (draw_h / 2) - 2;
            mvprintw(py, x + 4, "Koni can search the internet if the song does not have built-in");
            mvprintw(py+1, x + 4, "lyrics or is not available locally.");
            mvprintw(py+2, x + 4, "Allow Koni to connect to the internet for this purpose?");
            attron(A_BOLD | COLOR_PAIR(4));
            mvprintw(py+4, x + 4, "Press 'Y' for Yes, or 'N' for No.");
            attroff(A_BOLD | COLOR_PAIR(4));
            return;
        } else if (app_config.online_lyrics && !app_config.download_online_lyrics_asked) {
            int py = y + (draw_h / 2) - 2;
            mvprintw(py, x + 4, "Would you like to save downloaded lyrics locally");
            mvprintw(py+1, x + 4, "so they can be retrieved offline later?");
            attron(A_BOLD | COLOR_PAIR(4));
            mvprintw(py+3, x + 4, "Press 'Y' for Yes, or 'N' for No.");
            attroff(A_BOLD | COLOR_PAIR(4));
            return;
        }

        if (current_lyrics_backend[0] != '\0') {
            int b_len = utf8_display_width(current_lyrics_backend);
            if (draw_w > b_len + 4) {
                attron(A_DIM | COLOR_PAIR(2));
                mvprintw(y, x + w - b_len - 3, "[%s]", current_lyrics_backend);
                attroff(A_DIM | COLOR_PAIR(2));
            }
        }
        
        if (ui_cache.lrc_doc) {
            uint32_t current_ms = 0;
            uint32_t srate = atomic_load(&vis_srate);
            if (srate > 0) current_ms = (uint32_t)(((uint64_t)ui_cache.smooth_rpos * 1000) / srate);
            
            int active_idx = lyric_document_get_active_line(ui_cache.lrc_doc, current_ms);
            
            int visible_lines = draw_h;
            int scroll = 0;
            if (active_idx >= 0) {
                scroll = active_idx - visible_lines / 2;
            }
            if (scroll > ui_cache.lrc_doc->num_lines - visible_lines) {
                scroll = ui_cache.lrc_doc->num_lines - visible_lines;
            }
            if (scroll < 0) scroll = 0;
            
            for (int i = 0; i < visible_lines && i + scroll < ui_cache.lrc_doc->num_lines; i++) {
                int line_idx = i + scroll;
                bool is_active = (line_idx == active_idx);
                
                if (is_active) attron(A_BOLD | A_REVERSE | COLOR_PAIR(4));
                else attron(COLOR_PAIR(2));
                
                mvprintw(y + 2 + i, x + 4, "%.*s", draw_w - 2, ui_cache.lrc_doc->lines[line_idx].raw_text);
                
                if (is_active) attroff(A_BOLD | A_REVERSE | COLOR_PAIR(4));
                else attroff(COLOR_PAIR(2));
            }
        } else if (ui_cache.meta.lyrics) {
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