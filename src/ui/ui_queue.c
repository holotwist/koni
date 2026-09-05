#include "ui_common.h"
#include "ui_animations.h"
#include <string.h>

void draw_queue_panel(int y, int x, int h, int w) {
    if (h < 4 || w < 2) return;
    
    int start_y = y + 1;
    int list_h = h - 2;

    int search_offset = ui_search_render_bar(start_y, x, w);
    start_y += search_offset;
    list_h -= search_offset;

    static int filtered_map[8192];
    bool search_active = ui_search_is_active();
    int total_visible = search_active ? ui_search_get_filtered_indices(TAB_QUEUE, filtered_map, 8192) : num_playlist_files;

    int *cur_sel = search_active ? ui_search_get_selected_ptr() : &selected_playlist_idx;
    int *cur_scroll = search_active ? ui_search_get_scroll_ptr() : &playlist_scroll_offset;

    if (list_h > 0 && total_visible > 0) {
        if (*cur_sel >= total_visible) *cur_sel = total_visible - 1;
        if (*cur_sel < *cur_scroll) *cur_scroll = *cur_sel;
        else if (*cur_sel >= *cur_scroll + list_h) *cur_scroll = *cur_sel - list_h + 1;
    }

    for (int i = 0; i < list_h; i++) mvhline(start_y + i, x + 1, ' ', w - 2);

    if (ui_last_playlist_idx != *cur_sel) {
        ui_last_playlist_idx = *cur_sel;
        ui_frame_counter = 0;
    }

    int max_disp_len = w - 4;
    if (max_disp_len < 1) max_disp_len = 1;

    for (int i = 0; i < list_h && i + *cur_scroll < total_visible; i++) {
        int list_pos = i + *cur_scroll;
        int idx = search_active ? filtered_map[list_pos] : list_pos;
        bool is_playing = (current_play_source == SOURCE_QUEUE && playing_file_idx == idx);
        
        if (list_pos == *cur_sel) attron(A_REVERSE | COLOR_PAIR(1));
        else if (is_playing) attron(A_BOLD | COLOR_PAIR(4));
        else attron(COLOR_PAIR(2));
        
        char formatted_name[512] = {0};
        pthread_mutex_lock(&state_mutex);
        format_list_item(formatted_name, sizeof(formatted_name), max_disp_len, playlist[idx].name, 
                         playlist[idx].metadata_loaded ? &playlist[idx].meta : NULL, playlist[idx].duration_sec, false);
        pthread_mutex_unlock(&state_mutex);
                         
        char disp_buf[1024] = {0};
        int text_w = utf8_display_width(formatted_name);
        
        if (list_pos == *cur_sel) {
            get_marquee_text(formatted_name, text_w, max_disp_len, ui_frame_counter, disp_buf, sizeof(disp_buf));
        } else {
            get_marquee_text(formatted_name, text_w, max_disp_len, 0, disp_buf, sizeof(disp_buf));
        }
        
        int chars_copied = (text_w <= max_disp_len) ? text_w : max_disp_len;
        mvprintw(start_y + i, x + 2, "%s", disp_buf);
        for (int p = chars_copied; p < max_disp_len; p++) printw(" ");

        if (list_pos == *cur_sel) attroff(A_REVERSE | COLOR_PAIR(1));
        else if (is_playing) attroff(A_BOLD | COLOR_PAIR(4));
        else attroff(COLOR_PAIR(2));
    }
}