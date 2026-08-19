#include "ui_common.h"
#include "ui_animations.h"
#include <string.h>

void draw_playlist_panel(int y, int x, int h, int w) {
    if (h < 5 || w < 2) return;
    ui_draw_box(y, x, h, w, "playlist", (current_focus == FOCUS_PLAYLIST) ? 1 : 2);
    
    int list_h = h - 2;
    
    if (list_h > 0) {
        if (selected_playlist_idx < playlist_scroll_offset) {
            playlist_scroll_offset = selected_playlist_idx;
        } else if (selected_playlist_idx >= playlist_scroll_offset + list_h) {
            playlist_scroll_offset = selected_playlist_idx - list_h + 1;
        }
    }

    for (int i = 0; i < list_h; i++) mvhline(y + i + 1, x + 1, ' ', w - 2);

    if (ui_last_playlist_idx != selected_playlist_idx) {
        ui_last_playlist_idx = selected_playlist_idx;
        if (current_focus == FOCUS_PLAYLIST) ui_frame_counter = 0;
    }

    int max_disp_len = w - 4;
    if (max_disp_len < 1) max_disp_len = 1;

    for (int i = 0; i < list_h && i + playlist_scroll_offset < num_playlist_files; i++) {
        int idx = i + playlist_scroll_offset;
        
        if (idx == selected_playlist_idx && current_focus == FOCUS_PLAYLIST) attron(A_REVERSE | COLOR_PAIR(1));
        else if (idx == selected_playlist_idx) attron(A_REVERSE);
        else attron(COLOR_PAIR(2));
        
        char disp_buf[1024] = {0};
        if (idx == selected_playlist_idx && current_focus == FOCUS_PLAYLIST) {
            get_marquee_text(playlist[idx].name, max_disp_len, ui_frame_counter, disp_buf, sizeof(disp_buf));
        } else {
            get_marquee_text(playlist[idx].name, max_disp_len, 0, disp_buf, sizeof(disp_buf));
        }
        
        int chars_copied = utf8_display_width(disp_buf);
        mvprintw(y + i + 1, x + 2, "%s", disp_buf);
        
        for (int p = chars_copied; p < max_disp_len; p++) printw(" ");

        if (idx == selected_playlist_idx && current_focus == FOCUS_PLAYLIST) attroff(A_REVERSE | COLOR_PAIR(1));
        else if (idx == selected_playlist_idx) attroff(A_REVERSE);
        else attroff(COLOR_PAIR(2));
    }
}