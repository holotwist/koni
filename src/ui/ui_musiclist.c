#include "ui_common.h"
#include "ui_animations.h"
#include "db.h"
#include <string.h>

void draw_musiclist_panel(int y, int x, int h, int w) {
    if (h < 4 || w < 2) return;

    int sort_str_len = (int)strlen(db_get_sort_name(current_library_sort)) + 8;
    if (w > sort_str_len + 30) {
        attron(A_DIM | COLOR_PAIR(2));
        mvprintw(y, x + w - sort_str_len - 2, "[Sort: %s]", db_get_sort_name(current_library_sort));
        attroff(A_DIM | COLOR_PAIR(2));
    }

    int start_y = y + 1;
    int list_h = h - 2;

    int search_offset = ui_search_render_bar(start_y, x, w);
    start_y += search_offset;
    list_h -= search_offset;

    static int filtered_map[65536];
    int total_visible = ui_search_get_filtered_indices(TAB_MUSIC, filtered_map, 65536);

    int *cur_sel = ui_search_is_active() ? ui_search_get_selected_ptr() : &selected_library_idx;
    int *cur_scroll = ui_search_is_active() ? ui_search_get_scroll_ptr() : &library_scroll_offset;

    if (list_h > 0 && total_visible > 0) {
        if (*cur_sel >= total_visible) *cur_sel = total_visible - 1;
        if (*cur_sel < *cur_scroll) *cur_scroll = *cur_sel;
        else if (*cur_sel >= *cur_scroll + list_h) *cur_scroll = *cur_sel - list_h + 1;
    }

    for (int i = 0; i < list_h; i++) mvhline(start_y + i, x + 1, ' ', w - 2);

    int max_disp_len = w - 4;
    if (max_disp_len < 1) max_disp_len = 1;

    if (total_visible == 0) {
        if (ui_search_is_active() && ui_search_get_query()[0]) {
            mvprintw(start_y + 1, x + 3, "No matching tracks found.");
        } else if (library_scanner_is_running()) {
            mvprintw(start_y + 1, x + 3, "Scanning music library...");
        } else {
            mvprintw(start_y + 1, x + 3, "No tracks in library. Press 'u' to scan.");
        }
        return;
    }

    for (int i = 0; i < list_h && i + *cur_scroll < total_visible; i++) {
        int list_pos = i + *cur_scroll;
        int idx = filtered_map[list_pos];
        bool is_playing = (current_play_source == SOURCE_LIBRARY && playing_file_idx == idx);

        if (list_pos == *cur_sel) attron(A_REVERSE | COLOR_PAIR(1));
        else if (is_playing) attron(A_BOLD | COLOR_PAIR(4));
        else attron(COLOR_PAIR(2));

        char track_name[256] = {0};
        uint32_t dur = 0;
        KoniMetadata meta = {0};

        pthread_mutex_lock(&state_mutex);
        if (library_tracks && idx < num_library_tracks) {
            strncpy(track_name, library_tracks[idx].name, sizeof(track_name) - 1);
            dur = library_tracks[idx].duration_sec;
            meta.title = library_tracks[idx].title[0] ? library_tracks[idx].title : NULL;
            meta.artist = library_tracks[idx].artist[0] ? library_tracks[idx].artist : NULL;
            meta.album = library_tracks[idx].album[0] ? library_tracks[idx].album : NULL;
            meta.has_track_gain = library_tracks[idx].has_track_gain;
            meta.track_gain = library_tracks[idx].track_gain;
        }
        pthread_mutex_unlock(&state_mutex);

        char formatted_name[512] = {0};
        format_list_item(formatted_name, sizeof(formatted_name), max_disp_len, track_name, 
                         &meta, dur, false);

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