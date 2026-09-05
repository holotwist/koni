#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#define _XOPEN_SOURCE 600

#include "ui_search.h"
#include "ui_common.h"
#include <string.h>
#include <ncurses.h>

typedef struct {
    bool active;
    char query[128];
    int len;
    int selected_idx;
    int scroll_offset;
} SearchWidgetState;

static SearchWidgetState search_state = {0};

void ui_search_open(void) {
    search_state.active = true;
    search_state.query[0] = '\0';
    search_state.len = 0;
    search_state.selected_idx = 0;
    search_state.scroll_offset = 0;
}

void ui_search_close(void) {
    search_state.active = false;
    search_state.query[0] = '\0';
    search_state.len = 0;
    search_state.selected_idx = 0;
    search_state.scroll_offset = 0;
}

bool ui_search_is_active(void) {
    return search_state.active;
}

const char* ui_search_get_query(void) {
    return search_state.query;
}

int* ui_search_get_selected_ptr(void) {
    return &search_state.selected_idx;
}

int* ui_search_get_scroll_ptr(void) {
    return &search_state.scroll_offset;
}

static bool ci_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) return false;
    return strcasestr(haystack, needle) != NULL;
}

int ui_search_get_filtered_indices(BrowserTab tab, int *out_indices, int max_count) {
    if (!out_indices || max_count <= 0) return 0;

    const char *query = search_state.active ? search_state.query : "";
    if (!query[0]) {
        int total = 0;
        if (tab == TAB_MUSIC) total = num_library_tracks;
        else if (tab == TAB_FILES) total = num_files;
        else if (tab == TAB_QUEUE) total = num_playlist_files;
        if (total > max_count) total = max_count;
        for (int i = 0; i < total; i++) out_indices[i] = i;
        return total;
    }

    int count = 0;
    if (tab == TAB_MUSIC) {
        for (int i = 0; i < num_library_tracks && count < max_count; i++) {
            if (ci_contains(library_tracks[i].title, query) ||
                ci_contains(library_tracks[i].artist, query) ||
                ci_contains(library_tracks[i].album, query) ||
                ci_contains(library_tracks[i].name, query)) {
                out_indices[count++] = i;
            }
        }
    } else if (tab == TAB_FILES) {
        for (int i = 0; i < num_files && count < max_count; i++) {
            if (i == 0 && strcmp(files[i].name, "..") == 0) continue;
            if (ci_contains(files[i].name, query) ||
                ci_contains(files[i].meta.title, query) ||
                ci_contains(files[i].meta.artist, query) ||
                ci_contains(files[i].meta.album, query)) {
                out_indices[count++] = i;
            }
        }
    } else if (tab == TAB_QUEUE) {
        for (int i = 0; i < num_playlist_files && count < max_count; i++) {
            if (ci_contains(playlist[i].name, query) ||
                ci_contains(playlist[i].meta.title, query) ||
                ci_contains(playlist[i].meta.artist, query) ||
                ci_contains(playlist[i].meta.album, query)) {
                out_indices[count++] = i;
            }
        }
    }
    return count;
}

int ui_search_render_bar(int y, int x, int w) {
    if (!search_state.active || w < 4) return 0;
    mvhline(y, x + 1, ' ', w - 2);
    attron(COLOR_PAIR(4) | A_BOLD);
    mvprintw(y, x + 2, "/ %s", search_state.query);
    printw("%s", (ui_frame_counter % 30 < 15) ? "_" : " ");
    attroff(COLOR_PAIR(4) | A_BOLD);
    return 1;
}

bool ui_search_handle_input(int ch, BrowserTab tab) {
    if (!search_state.active) return false;

    if (ch == 27) { // ESC, exit and clear
        ui_search_close();
        return true;
    }

    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
        if (search_state.len > 0) {
            search_state.query[--search_state.len] = '\0';
            search_state.selected_idx = 0;
            search_state.scroll_offset = 0;
        }
        return true;
    }

    if (ch == KEY_UP) {
        if (search_state.selected_idx > 0) search_state.selected_idx--;
        return true;
    }

    if (ch == KEY_DOWN) {
        static int map[65536];
        int count = ui_search_get_filtered_indices(tab, map, 65536);
        if (search_state.selected_idx < count - 1) search_state.selected_idx++;
        return true;
    }

    if (ch == KEY_PPAGE) {
        search_state.selected_idx -= 10;
        if (search_state.selected_idx < 0) search_state.selected_idx = 0;
        return true;
    }

    if (ch == KEY_NPAGE) {
        static int map[65536];
        int count = ui_search_get_filtered_indices(tab, map, 65536);
        search_state.selected_idx += 10;
        if (search_state.selected_idx >= count) search_state.selected_idx = (count > 0) ? count - 1 : 0;
        return true;
    }

    if (ch == KEY_HOME) {
        search_state.selected_idx = 0;
        return true;
    }

    if (ch == KEY_END) {
        static int map[65536];
        int count = ui_search_get_filtered_indices(tab, map, 65536);
        search_state.selected_idx = (count > 0) ? count - 1 : 0;
        return true;
    }

    if (ch == 10) { // Enter, select item and close search
        static int map[65536];
        int count = ui_search_get_filtered_indices(tab, map, 65536);
        if (count > 0 && search_state.selected_idx < count) {
            int target_idx = map[search_state.selected_idx];
            if (tab == TAB_MUSIC) selected_library_idx = target_idx;
            else if (tab == TAB_FILES) selected_file_idx = target_idx;
            else if (tab == TAB_QUEUE) selected_playlist_idx = target_idx;
        }
        ui_search_close();
        return false; // Let Enter fall through to trigger the actual playback
    }

    if (ch >= 32 && ch <= 126) {
        if (search_state.len < (int)sizeof(search_state.query) - 1) {
            search_state.query[search_state.len++] = (char)ch;
            search_state.query[search_state.len] = '\0';
            search_state.selected_idx = 0;
            search_state.scroll_offset = 0;
        }
        return true;
    }

    return true;
}