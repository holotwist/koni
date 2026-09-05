#include "ui_common.h"
#include "ui_animations.h"
#include "file_list.h"
#include "config.h"
#include <string.h>

void draw_files_panel(int y, int x, int h, int w) {
    if (h < 5 || w < 2) return;
    
    int start_y = y + 3;
    int list_h = h - 4;

    int max_path_width = w - 3;
    if (max_path_width > 0) {
        int path_width = utf8_display_width(current_dir);
        if (path_width <= max_path_width) {
            mvprintw(y + 1, x + 1, " %s", current_dir);
        } else if (max_path_width > 3) {
            int offset = utf8_byte_offset_for_suffix(current_dir, max_path_width - 3);
            mvprintw(y + 1, x + 1, " ...%s", current_dir + offset);
        } else {
            mvprintw(y + 1, x + 1, " ...");
        }
    }
    
    attron(COLOR_PAIR(4)); 
    mvhline(y + 2, x + 1, ACS_HLINE, w - 2); 
    attroff(COLOR_PAIR(4));

    int search_offset = ui_search_render_bar(start_y, x, w);
    start_y += search_offset;
    list_h -= search_offset;

    static int filtered_map[8192];
    bool search_active = ui_search_is_active();
    int total_visible = search_active ? ui_search_get_filtered_indices(TAB_FILES, filtered_map, 8192) : num_files;

    int *cur_sel = search_active ? ui_search_get_selected_ptr() : &selected_file_idx;
    int *cur_scroll = search_active ? ui_search_get_scroll_ptr() : &scroll_offset;

    if (list_h > 0 && total_visible > 0) {
        if (*cur_sel >= total_visible) *cur_sel = total_visible - 1;
        if (*cur_sel < *cur_scroll) *cur_scroll = *cur_sel;
        else if (*cur_sel >= *cur_scroll + list_h) *cur_scroll = *cur_sel - list_h + 1;
    }

    for (int i = 0; i < list_h; i++) mvhline(start_y + i, x + 1, ' ', w - 2);
    
    if (ui_last_selected_idx != *cur_sel) {
        ui_last_selected_idx = *cur_sel;
        ui_frame_counter = 0;
    }

    int max_disp_len = w - 4;
    if (max_disp_len < 1) max_disp_len = 1;

    for (int i = 0; i < list_h && i + *cur_scroll < total_visible; i++) {
        int list_pos = i + *cur_scroll;
        int idx = search_active ? filtered_map[list_pos] : list_pos;
        
        char item_path[1024];
        snprintf(item_path, sizeof(item_path), "%s/%s", current_dir, files[idx].name);
        bool is_playing = (current_play_source == SOURCE_FILES && strcmp(playing_filepath, item_path) == 0);
        bool is_selected_dir = files[idx].is_dir && config_is_music_dir(item_path);

        if (list_pos == *cur_sel) attron(A_REVERSE | COLOR_PAIR(1));
        else if (is_playing) attron(A_BOLD | COLOR_PAIR(4));
        else if (files[idx].is_dir) attron(COLOR_PAIR(3));
        else attron(COLOR_PAIR(2));
        
        char formatted_name[512] = {0};

        pthread_mutex_lock(&state_mutex);
        if (is_selected_dir) {
            char tagged_name[300];
            snprintf(tagged_name, sizeof(tagged_name), "%s  (Selected)", files[idx].name);
            format_list_item(formatted_name, sizeof(formatted_name), max_disp_len, tagged_name, 
                             NULL, 0, true);
        } else {
            format_list_item(formatted_name, sizeof(formatted_name), max_disp_len, files[idx].name, 
                             files[idx].metadata_loaded ? &files[idx].meta : NULL, files[idx].duration_sec, files[idx].is_dir);
        }
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
        else if (files[idx].is_dir) attroff(COLOR_PAIR(3));
        else attroff(COLOR_PAIR(2));
    }
}