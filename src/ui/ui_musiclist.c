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

    int list_h = h - 2;
    if (list_h > 0) {
        if (selected_library_idx < library_scroll_offset) {
            library_scroll_offset = selected_library_idx;
        } else if (selected_library_idx >= library_scroll_offset + list_h) {
            library_scroll_offset = selected_library_idx - list_h + 1;
        }
    }

    for (int i = 0; i < list_h; i++) mvhline(y + i + 1, x + 1, ' ', w - 2);

    int max_disp_len = w - 4;
    if (max_disp_len < 1) max_disp_len = 1;

    if (num_library_tracks == 0) {
        if (library_scanner_is_running()) {
            mvprintw(y + 2, x + 3, "Scanning music library...");
        } else {
            mvprintw(y + 2, x + 3, "No tracks in library. Press 'u' to scan.");
        }
        return;
    }

    for (int i = 0; i < list_h && i + library_scroll_offset < num_library_tracks; i++) {
        int idx = i + library_scroll_offset;
        bool is_playing = (current_play_source == SOURCE_LIBRARY && playing_file_idx == idx);

        if (idx == selected_library_idx) attron(A_REVERSE | COLOR_PAIR(1));
        else if (is_playing) attron(A_BOLD | COLOR_PAIR(4));
        else attron(COLOR_PAIR(2));

        KoniMetadata meta = {
            .title = library_tracks[idx].title[0] ? library_tracks[idx].title : NULL,
            .artist = library_tracks[idx].artist[0] ? library_tracks[idx].artist : NULL,
            .album = library_tracks[idx].album[0] ? library_tracks[idx].album : NULL,
            .has_track_gain = library_tracks[idx].has_track_gain,
            .track_gain = library_tracks[idx].track_gain
        };

        char formatted_name[512] = {0};
        format_list_item(formatted_name, sizeof(formatted_name), max_disp_len, library_tracks[idx].name, 
                         &meta, library_tracks[idx].duration_sec, false);

        char disp_buf[1024] = {0};
        int text_w = utf8_display_width(formatted_name);

        if (idx == selected_library_idx) {
            get_marquee_text(formatted_name, text_w, max_disp_len, ui_frame_counter, disp_buf, sizeof(disp_buf));
        } else {
            get_marquee_text(formatted_name, text_w, max_disp_len, 0, disp_buf, sizeof(disp_buf));
        }

        int chars_copied = (text_w <= max_disp_len) ? text_w : max_disp_len;
        mvprintw(y + i + 1, x + 2, "%s", disp_buf);
        for (int p = chars_copied; p < max_disp_len; p++) printw(" ");

        if (idx == selected_library_idx) attroff(A_REVERSE | COLOR_PAIR(1));
        else if (is_playing) attroff(A_BOLD | COLOR_PAIR(4));
        else attroff(COLOR_PAIR(2));
    }
}