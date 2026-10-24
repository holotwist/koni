#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "ui_common.h"
#include "ui_animations.h"
#include "playlist_manager.h"
#include "state.h"
#include <string.h>

static LoadedPlaylist s_drilldown_cache = {0};
static char s_cached_name[128] = {0};

void ui_playlists_invalidate_cache(void) {
    s_cached_name[0] = '\0';
}

const LoadedPlaylist* ui_playlists_get_current_loaded(void) {
    if (active_playlist_name[0] != '\0' && strcmp(s_cached_name, active_playlist_name) != 0) {
        playlist_mgmt_free_loaded(&s_drilldown_cache);
        playlist_mgmt_load_playlist(active_playlist_name, &s_drilldown_cache);
        strncpy(s_cached_name, active_playlist_name, sizeof(s_cached_name) - 1);
    }
    return &s_drilldown_cache;
}

void draw_playlists_panel(int y, int x, int h, int w) {
    if (h < 4 || w < 2) return;

    int start_y = y + 1;
    int list_h = h - 2;

    if (!playlist_in_drilldown) {
        int search_offset = ui_search_render_bar(start_y, x, w);
        start_y += search_offset;
        list_h -= search_offset;

        static int filtered_map[8192];
        bool search_active = ui_search_is_active();
        int total_playlists = playlist_mgmt_get_count();
        int total_visible = search_active ? ui_search_get_filtered_indices(TAB_PLAYLISTS, filtered_map, 8192) : total_playlists;

        int *cur_sel = search_active ? ui_search_get_selected_ptr() : &selected_playlist_browser_idx;
        int *cur_scroll = search_active ? ui_search_get_scroll_ptr() : &playlist_browser_scroll_offset;

        if (list_h > 0 && total_visible > 0) {
            if (*cur_sel >= total_visible) *cur_sel = total_visible - 1;
            if (*cur_sel < *cur_scroll) *cur_scroll = *cur_sel;
            else if (*cur_sel >= *cur_scroll + list_h) *cur_scroll = *cur_sel - list_h + 1;
        }

        for (int i = 0; i < list_h; i++) mvhline(start_y + i, x + 1, ' ', w - 2);

        int max_disp_len = w - 4;
        if (max_disp_len < 1) max_disp_len = 1;

        if (total_visible == 0) {
            if (search_active && ui_search_get_query()[0]) {
                mvprintw(start_y + 1, x + 3, "No matching playlists found.");
            } else {
                mvprintw(start_y + 1, x + 3, "No playlists found. Press 'i' on a track to create one.");
            }
            return;
        }

        for (int i = 0; i < list_h && i + *cur_scroll < total_visible; i++) {
            int list_pos = i + *cur_scroll;
            int idx = search_active ? filtered_map[list_pos] : list_pos;
            const PlaylistSummary *ps = playlist_mgmt_get_summary(idx);
            if (!ps) continue;

            bool is_active_playing_pl = (current_play_source == SOURCE_PLAYLIST &&
                                         strcmp(active_playlist_playback.name, ps->name) == 0);

            if (list_pos == *cur_sel) attron(A_REVERSE | COLOR_PAIR(1));
            else if (is_active_playing_pl) attron(A_BOLD | COLOR_PAIR(4));
            else if (ps->is_favourites) attron(COLOR_PAIR(4) | A_BOLD);
            else attron(COLOR_PAIR(2));

            char line_str[512];
            char count_str[32];
            snprintf(count_str, sizeof(count_str), "[%d %s]", ps->track_count, ps->track_count == 1 ? "track" : "tracks");

            const char *prefix = ps->is_favourites ? "★ " : "  ";
            int name_avail = max_disp_len - utf8_display_width(count_str) - utf8_display_width(prefix) - 1;
            if (name_avail < 5) name_avail = 5;

            char name_buf[256];
            int n_bytes = utf8_byte_offset_for_width(ps->name, name_avail);
            snprintf(name_buf, sizeof(name_buf), "%.*s", n_bytes, ps->name);
            int pad = name_avail - utf8_display_width(name_buf);
            if (pad < 0) pad = 0;

            snprintf(line_str, sizeof(line_str), "%s%s%*s %s", prefix, name_buf, pad, "", count_str);
            int line_w = utf8_display_width(line_str);
            mvprintw(start_y + i, x + 2, "%s", line_str);
            for (int p = line_w; p < max_disp_len; p++) printw(" ");

            if (list_pos == *cur_sel) attroff(A_REVERSE | COLOR_PAIR(1));
            else if (is_active_playing_pl) attroff(A_BOLD | COLOR_PAIR(4));
            else if (ps->is_favourites) attroff(COLOR_PAIR(4) | A_BOLD);
            else attron(COLOR_PAIR(2));
        }
    } else {
        const LoadedPlaylist *pl = ui_playlists_get_current_loaded();

        // Subheader navigation bar
        mvhline(start_y, x + 1, ' ', w - 2);
        attron(COLOR_PAIR(4) | A_BOLD);
        mvprintw(start_y, x + 2, "<- [..] %s (%d tracks)", active_playlist_name, pl->count);
        attroff(COLOR_PAIR(4) | A_BOLD);

        start_y++;
        list_h--;

        int search_offset = ui_search_render_bar(start_y, x, w);
        start_y += search_offset;
        list_h -= search_offset;

        static int filtered_map[8192];
        bool search_active = ui_search_is_active();
        int total_tracks = pl->count;
        int total_visible = search_active ? ui_search_get_filtered_indices(TAB_PLAYLISTS, filtered_map, 8192) : total_tracks;

        int *cur_sel = search_active ? ui_search_get_selected_ptr() : &selected_playlist_track_idx;
        int *cur_scroll = search_active ? ui_search_get_scroll_ptr() : &playlist_track_scroll_offset;

        if (list_h > 0 && total_visible > 0) {
            if (*cur_sel >= total_visible) *cur_sel = total_visible - 1;
            if (*cur_sel < *cur_scroll) *cur_scroll = *cur_sel;
            else if (*cur_sel >= *cur_scroll + list_h) *cur_scroll = *cur_sel - list_h + 1;
        }

        for (int i = 0; i < list_h; i++) mvhline(start_y + i, x + 1, ' ', w - 2);

        int max_disp_len = w - 4;
        if (max_disp_len < 1) max_disp_len = 1;

        if (total_tracks == 0) {
            mvprintw(start_y + 1, x + 3, "Playlist is empty. Press 'i' on any track to add it here.");
            return;
        }
        if (total_visible == 0) {
            mvprintw(start_y + 1, x + 3, "No matching tracks found.");
            return;
        }

        for (int i = 0; i < list_h && i + *cur_scroll < total_visible; i++) {
            int list_pos = i + *cur_scroll;
            int idx = search_active ? filtered_map[list_pos] : list_pos;
            const PlaylistTrackItem *ti = &pl->items[idx];

            bool is_playing = (current_play_source == SOURCE_PLAYLIST &&
                               strcmp(playing_filepath, ti->path) == 0);
            bool is_fav = playlist_mgmt_is_favourite(ti->path);

            if (list_pos == *cur_sel) attron(A_REVERSE | COLOR_PAIR(1));
            else if (is_playing) attron(A_BOLD | COLOR_PAIR(4));
            else attron(COLOR_PAIR(2));

            KoniMetadata meta = {0};
            meta.title = (char*)ti->title;
            meta.artist = (char*)ti->artist;

            const char *display_name = ti->title[0] ? ti->title : ti->path;
            const char *slash = strrchr(display_name, '/');
            if (slash) display_name = slash + 1;

            char formatted[512];
            format_list_item(formatted, sizeof(formatted), max_disp_len, display_name, &meta, ti->duration_sec, false, is_fav);

            char disp_buf[1024] = {0};
            int text_w = utf8_display_width(formatted);

            if (list_pos == *cur_sel) {
                get_marquee_text(formatted, text_w, max_disp_len, ui_frame_counter, disp_buf, sizeof(disp_buf));
            } else {
                get_marquee_text(formatted, text_w, max_disp_len, 0, disp_buf, sizeof(disp_buf));
            }

            int disp_w = utf8_display_width(disp_buf);
            int print_bytes = utf8_byte_offset_for_width(disp_buf, max_disp_len);
            mvprintw(start_y + i, x + 2, "%.*s", print_bytes, disp_buf);
            for (int p = disp_w; p < max_disp_len; p++) printw(" ");

            if (list_pos == *cur_sel) attroff(A_REVERSE | COLOR_PAIR(1));
            else if (is_playing) attroff(A_BOLD | COLOR_PAIR(4));
            else attroff(COLOR_PAIR(2));
        }
    }
}