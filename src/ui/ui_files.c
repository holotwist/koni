#include "ui_common.h"
#include "ui_animations.h"
#include "file_list.h"
#include <string.h>

void draw_files_panel(int y, int x, int h, int w) {
    if (h < 5 || w < 2) return;
    ui_draw_box(y, x, h, w, "files", (current_focus == FOCUS_FILES) ? 1 : 2);
    
    int list_h = h - 4;
    
    // Auto-follow cursor
    if (list_h > 0) {
        if (selected_file_idx < scroll_offset) {
            scroll_offset = selected_file_idx;
        } else if (selected_file_idx >= scroll_offset + list_h) {
            scroll_offset = selected_file_idx - list_h + 1;
        }
    }

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
    
    for (int i = 0; i < list_h; i++) mvhline(y + i + 3, x + 1, ' ', w - 2);
    
    if (ui_last_selected_idx != selected_file_idx) {
        ui_last_selected_idx = selected_file_idx;
        ui_frame_counter = 0; // Reset scroll animation
    }

    int max_disp_len = w - 4;
    if (max_disp_len < 1) max_disp_len = 1;

    for (int i = 0; i < list_h && i + scroll_offset < num_files; i++) {
        int idx = i + scroll_offset;
        
        if (idx == selected_file_idx && current_focus == FOCUS_FILES) attron(A_REVERSE | COLOR_PAIR(1));
        else if (idx == selected_file_idx) attron(A_REVERSE);
        else if (files[idx].is_dir) attron(COLOR_PAIR(3));
        else attron(COLOR_PAIR(2));
        
        char formatted_name[512] = {0};
        pthread_mutex_lock(&state_mutex);
        format_list_item(formatted_name, sizeof(formatted_name), max_disp_len, files[idx].name, 
                         files[idx].metadata_loaded ? &files[idx].meta : NULL, files[idx].duration_sec, files[idx].is_dir);
        pthread_mutex_unlock(&state_mutex);
        
        char disp_buf[1024] = {0};
        int text_w = utf8_display_width(formatted_name);
        
        if (idx == selected_file_idx && current_focus == FOCUS_FILES) {
            get_marquee_text(formatted_name, text_w, max_disp_len, ui_frame_counter, disp_buf, sizeof(disp_buf));
        } else {
            get_marquee_text(formatted_name, text_w, max_disp_len, 0, disp_buf, sizeof(disp_buf));
        }
        
        int chars_copied = (text_w <= max_disp_len) ? text_w : max_disp_len;
        mvprintw(y + i + 3, x + 2, "%s", disp_buf);
        
        for (int p = chars_copied; p < max_disp_len; p++) printw(" ");

        if (idx == selected_file_idx && current_focus == FOCUS_FILES) attroff(A_REVERSE | COLOR_PAIR(1));
        else if (idx == selected_file_idx) attroff(A_REVERSE);
        else if (files[idx].is_dir) attroff(COLOR_PAIR(3));
        else attroff(COLOR_PAIR(2));
    }
}