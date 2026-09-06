#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "ui_modal.h"
#include "ui_common.h"
#include "ui_status.h"
#include "playlist_manager.h"
#include "state.h"
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>

typedef enum {
    TEXT_INPUT_CREATE_PLAYLIST = 0,
    TEXT_INPUT_RENAME_PLAYLIST
} TextInputPurpose;

static ModalType s_modal_type = MODAL_NONE;
static ModalTrackContext s_track_ctx = {0};
static char s_target_playlist[128] = {0};

static int s_selected_item = 0;
static int s_scroll_offset = 0;

// Text input state
static char s_input_prompt[128] = {0};
static char s_input_buffer[128] = {0};
static int s_input_len = 0;
static TextInputPurpose s_input_purpose = TEXT_INPUT_CREATE_PLAYLIST;

void ui_modal_init(void) {
    s_modal_type = MODAL_NONE;
    s_selected_item = 0;
    s_scroll_offset = 0;
}

bool ui_modal_is_active(void) {
    return s_modal_type != MODAL_NONE;
}

void ui_modal_close(void) {
    s_modal_type = MODAL_NONE;
    force_redraw = true;
}

void ui_modal_open_track_actions(const ModalTrackContext *ctx) {
    if (!ctx) return;
    s_track_ctx = *ctx;
    s_modal_type = MODAL_TRACK_ACTIONS;
    s_selected_item = 0;
    s_scroll_offset = 0;
    force_redraw = true;
}

void ui_modal_open_playlist_actions(const char *playlist_name) {
    if (!playlist_name) return;
    strncpy(s_target_playlist, playlist_name, sizeof(s_target_playlist) - 1);
    s_modal_type = MODAL_PLAYLIST_ACTIONS;
    s_selected_item = 0;
    s_scroll_offset = 0;
    force_redraw = true;
}

static void open_add_to_playlist_modal(void) {
    s_modal_type = MODAL_ADD_TO_PLAYLIST;
    s_selected_item = 0;
    s_scroll_offset = 0;
    force_redraw = true;
}

static void open_text_input(const char *prompt, const char *initial, TextInputPurpose purpose) {
    s_modal_type = MODAL_TEXT_INPUT;
    strncpy(s_input_prompt, prompt, sizeof(s_input_prompt) - 1);
    strncpy(s_input_buffer, initial ? initial : "", sizeof(s_input_buffer) - 1);
    s_input_len = (int)strlen(s_input_buffer);
    s_input_purpose = purpose;
    force_redraw = true;
}

static void execute_play_next(const char *path, const char *title) {
    pthread_mutex_lock(&state_mutex);
    if (num_playlist_files >= playlist_capacity) {
        playlist_capacity = playlist_capacity == 0 ? 1024 : playlist_capacity * 2;
        playlist = realloc(playlist, sizeof(PlaylistEntry) * playlist_capacity);
    }
    // Shift items to insert at position 0 (or position 1 if queue is currently playing)
    int insert_pos = (current_play_source == SOURCE_QUEUE && num_playlist_files > 0) ? 1 : 0;
    for (int i = num_playlist_files; i > insert_pos; i--) {
        playlist[i] = playlist[i - 1];
    }
    strncpy(playlist[insert_pos].path, path, sizeof(playlist[insert_pos].path) - 1);
    const char *slash = strrchr(path, '/');
    strncpy(playlist[insert_pos].name, (title && title[0]) ? title : (slash ? slash + 1 : path), 255);
    playlist[insert_pos].display_width = utf8_display_width(playlist[insert_pos].name);
    memset(&playlist[insert_pos].meta, 0, sizeof(KoniMetadata));
    playlist[insert_pos].metadata_loaded = false;
    playlist[insert_pos].duration_sec = s_track_ctx.duration_sec;
    num_playlist_files++;
    pthread_mutex_unlock(&state_mutex);

    ui_status_set("Queued to Play Next: %.30s", playlist[insert_pos].name);
}

static void execute_add_to_queue(const char *path, const char *title) {
    pthread_mutex_lock(&state_mutex);
    if (num_playlist_files >= playlist_capacity) {
        playlist_capacity = playlist_capacity == 0 ? 1024 : playlist_capacity * 2;
        playlist = realloc(playlist, sizeof(PlaylistEntry) * playlist_capacity);
    }
    int idx = num_playlist_files;
    strncpy(playlist[idx].path, path, sizeof(playlist[idx].path) - 1);
    const char *slash = strrchr(path, '/');
    strncpy(playlist[idx].name, (title && title[0]) ? title : (slash ? slash + 1 : path), 255);
    playlist[idx].display_width = utf8_display_width(playlist[idx].name);
    memset(&playlist[idx].meta, 0, sizeof(KoniMetadata));
    playlist[idx].metadata_loaded = false;
    playlist[idx].duration_sec = s_track_ctx.duration_sec;
    num_playlist_files++;
    pthread_mutex_unlock(&state_mutex);

    ui_status_set("Added to Queue: %.30s", playlist[idx].name);
}

static void draw_modal_box(int y, int x, int h, int w, const char *title) {
    for (int row = 0; row < h; row++) mvhline(y + row, x, ' ', w);
    ui_draw_box(y, x, h, w, title, 4);
}

static void render_track_actions(int max_y, int max_x) {
    int w = 50;
    int h = s_track_ctx.in_playlist ? 13 : 12;
    int x = (max_x - w) / 2;
    int y = (max_y - h) / 2;

    const char *title_disp = (s_track_ctx.title[0]) ? s_track_ctx.title : "Track";
    char box_title[64];
    snprintf(box_title, sizeof(box_title), "Actions: %.30s", title_disp);
    draw_modal_box(y, x, h, w, box_title);

    bool is_fav = playlist_mgmt_is_favourite(s_track_ctx.path);
    char fav_str[64];
    snprintf(fav_str, sizeof(fav_str), "[5] Toggle Favourite [%s]", is_fav ? "★" : " ");

    const char *items[7];
    items[0] = "[1] View Track Details / Tags";
    items[1] = "[2] Play Next";
    items[2] = "[3] Add to Queue";
    items[3] = "[4] Add to Playlist >";
    items[4] = fav_str;
    int total_items = 5;

    if (s_track_ctx.in_playlist) {
        items[5] = "[6] Remove from Playlist";
        total_items = 6;
    }

    for (int i = 0; i < total_items; i++) {
        if (i == s_selected_item) {
            attron(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
            mvprintw(y + 2 + i, x + 3, " %-42s ", items[i]);
            attroff(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
        } else {
            attron(COLOR_PAIR(2));
            mvprintw(y + 2 + i, x + 3, "   %-40s ", items[i]);
            attroff(COLOR_PAIR(2));
        }
    }

    attron(A_DIM | COLOR_PAIR(2));
    mvprintw(y + h - 2, x + 3, "[Esc] Cancel");
    attroff(A_DIM | COLOR_PAIR(2));
}

static void render_add_to_playlist(int max_y, int max_x) {
    int pl_count = playlist_mgmt_get_count();
    int total_options = pl_count + 1; // 0 = + [Create New Playlist], 1..pl_count = playlists

    int w = 52;
    int h = 14;
    int x = (max_x - w) / 2;
    int y = (max_y - h) / 2;

    draw_modal_box(y, x, h, w, "Add to Playlist");

    int list_h = h - 4;
    if (s_selected_item < s_scroll_offset) s_scroll_offset = s_selected_item;
    if (s_selected_item >= s_scroll_offset + list_h) s_scroll_offset = s_selected_item - list_h + 1;

    for (int i = 0; i < list_h && (i + s_scroll_offset) < total_options; i++) {
        int idx = i + s_scroll_offset;
        char line_buf[64];

        if (idx == 0) {
            snprintf(line_buf, sizeof(line_buf), "+ [Create New Playlist]");
        } else {
            const PlaylistSummary *ps = playlist_mgmt_get_summary(idx - 1);
            if (ps) {
                if (ps->is_favourites) {
                    snprintf(line_buf, sizeof(line_buf), "★ %-30s [%d]", ps->name, ps->track_count);
                } else {
                    snprintf(line_buf, sizeof(line_buf), "  %-30s [%d]", ps->name, ps->track_count);
                }
            } else {
                snprintf(line_buf, sizeof(line_buf), "  Unknown");
            }
        }

        if (idx == s_selected_item) {
            attron(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
            mvprintw(y + 2 + i, x + 3, " %-44s ", line_buf);
            attroff(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
        } else {
            attron(COLOR_PAIR(2));
            mvprintw(y + 2 + i, x + 3, "   %-42s ", line_buf);
            attroff(COLOR_PAIR(2));
        }
    }

    attron(A_DIM | COLOR_PAIR(2));
    mvprintw(y + h - 2, x + 3, "[Esc] Back");
    attroff(A_DIM | COLOR_PAIR(2));
}

static void render_text_input(int max_y, int max_x) {
    int w = 50;
    int h = 7;
    int x = (max_x - w) / 2;
    int y = (max_y - h) / 2;

    draw_modal_box(y, x, h, w, s_input_prompt);

    attron(COLOR_PAIR(2));
    mvprintw(y + 2, x + 3, "> %s", s_input_buffer);
    attroff(COLOR_PAIR(2));

    attron(A_BOLD | COLOR_PAIR(4));
    printw("_");
    attroff(A_BOLD | COLOR_PAIR(4));

    attron(A_DIM | COLOR_PAIR(2));
    mvprintw(y + 4, x + 3, "[Enter] Confirm   [Esc] Cancel");
    attroff(A_DIM | COLOR_PAIR(2));
}

static void render_track_details(int max_y, int max_x) {
    int w = 62;
    int h = 13;
    int x = (max_x - w) / 2;
    int y = (max_y - h) / 2;

    draw_modal_box(y, x, h, w, "Track Details");

    int line = y + 2;
    attron(COLOR_PAIR(2));
    mvprintw(line++, x + 3, "Title    : %-42.42s", s_track_ctx.title[0] ? s_track_ctx.title : "<Empty>");
    mvprintw(line++, x + 3, "Artist   : %-42.42s", s_track_ctx.artist[0] ? s_track_ctx.artist : "<Empty>");
    mvprintw(line++, x + 3, "Album    : %-42.42s", s_track_ctx.album[0] ? s_track_ctx.album : "<Empty>");
    mvprintw(line++, x + 3, "Duration : %02u:%02u", s_track_ctx.duration_sec / 60, s_track_ctx.duration_sec % 60);
    mvprintw(line++, x + 3, "Favourite: %s", playlist_mgmt_is_favourite(s_track_ctx.path) ? "Yes (★)" : "No");

    char short_path[45];
    int p_w = utf8_display_width(s_track_ctx.path);
    if (p_w > 42) {
        int off = utf8_byte_offset_for_suffix(s_track_ctx.path, 39);
        snprintf(short_path, sizeof(short_path), "...%s", s_track_ctx.path + off);
    } else {
        snprintf(short_path, sizeof(short_path), "%s", s_track_ctx.path);
    }
    mvprintw(line++, x + 3, "File     : %-42s", short_path);
    attroff(COLOR_PAIR(2));

    attron(A_DIM | COLOR_PAIR(2));
    mvprintw(y + h - 2, x + 3, "[Esc / Enter] Close");
    attroff(A_DIM | COLOR_PAIR(2));
}

static void render_playlist_actions(int max_y, int max_x) {
    int w = 48;
    int h = 11;
    int x = (max_x - w) / 2;
    int y = (max_y - h) / 2;

    char box_title[64];
    snprintf(box_title, sizeof(box_title), "Playlist: %.25s", s_target_playlist);
    draw_modal_box(y, x, h, w, box_title);

    const char *items[4] = {
        "[1] Play Playlist",
        "[2] Add All to Queue",
        "[3] Rename Playlist",
        "[4] Delete Playlist"
    };

    for (int i = 0; i < 4; i++) {
        if (i == s_selected_item) {
            attron(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
            mvprintw(y + 2 + i, x + 3, " %-38s ", items[i]);
            attroff(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
        } else {
            attron(COLOR_PAIR(2));
            mvprintw(y + 2 + i, x + 3, "   %-36s ", items[i]);
            attroff(COLOR_PAIR(2));
        }
    }

    attron(A_DIM | COLOR_PAIR(2));
    mvprintw(y + h - 2, x + 3, "[Esc] Cancel");
    attroff(A_DIM | COLOR_PAIR(2));
}

void ui_modal_render(int max_y, int max_x) {
    if (s_modal_type == MODAL_NONE) return;
    switch (s_modal_type) {
        case MODAL_TRACK_ACTIONS:    render_track_actions(max_y, max_x); break;
        case MODAL_ADD_TO_PLAYLIST: render_add_to_playlist(max_y, max_x); break;
        case MODAL_TEXT_INPUT:       render_text_input(max_y, max_x); break;
        case MODAL_TRACK_DETAILS:    render_track_details(max_y, max_x); break;
        case MODAL_PLAYLIST_ACTIONS: render_playlist_actions(max_y, max_x); break;
        default: break;
    }
}

static void handle_track_action_select(int opt) {
    switch (opt) {
        case 0: // View Details
            s_modal_type = MODAL_TRACK_DETAILS;
            force_redraw = true;
            break;
        case 1: // Play Next
            execute_play_next(s_track_ctx.path, s_track_ctx.title);
            ui_modal_close();
            break;
        case 2: // Add to Queue
            execute_add_to_queue(s_track_ctx.path, s_track_ctx.title);
            ui_modal_close();
            break;
        case 3: // Add to Playlist >
            open_add_to_playlist_modal();
            break;
        case 4: { // Toggle Favourite
            bool now_fav = playlist_mgmt_toggle_favourite(s_track_ctx.path, s_track_ctx.title, s_track_ctx.artist, s_track_ctx.duration_sec);
            ui_status_set(now_fav ? "Added to Favourites ★" : "Removed from Favourites");
            ui_modal_close();
            break;
        }
        case 5: // Remove from Playlist (only if in_playlist)
            if (s_track_ctx.in_playlist && s_track_ctx.playlist_name[0]) {
                playlist_mgmt_remove_track(s_track_ctx.playlist_name, s_track_ctx.playlist_track_idx);
                ui_status_set("Removed track from %s", s_track_ctx.playlist_name);
            }
            ui_modal_close();
            break;
    }
}

static void handle_add_to_playlist_select(int opt) {
    if (opt == 0) { // + [Create New Playlist]
        open_text_input("New Playlist Name", "", TEXT_INPUT_CREATE_PLAYLIST);
    } else {
        const PlaylistSummary *ps = playlist_mgmt_get_summary(opt - 1);
        if (ps) {
            bool ok = playlist_mgmt_add_track(ps->name, s_track_ctx.path, s_track_ctx.title, s_track_ctx.artist, s_track_ctx.duration_sec);
            if (ok) ui_status_set("Added to %s", ps->name);
            else ui_status_set("Already in %s", ps->name);
        }
        ui_modal_close();
    }
}

static void handle_playlist_action_select(int opt) {
    if (opt == 0) { // Play Playlist
        LoadedPlaylist lp;
        if (playlist_mgmt_load_playlist(s_target_playlist, &lp) && lp.count > 0) {
            pthread_mutex_lock(&state_mutex);
            strncpy(active_playlist_name, s_target_playlist, sizeof(active_playlist_name) - 1);
            current_play_source = SOURCE_PLAYLIST;
            playing_file_idx = 0;
            strncpy(playing_filepath, lp.items[0].path, sizeof(playing_filepath) - 1);
            strncpy(playing_filename, lp.items[0].title[0] ? lp.items[0].title : lp.items[0].path, 255);
            history_len = 0; history_idx = -1;
            pthread_mutex_unlock(&state_mutex);
            atomic_store(&current_cmd_atomic, CMD_PLAY);
            ui_status_set("Playing playlist: %s", s_target_playlist);
        }
        playlist_mgmt_free_loaded(&lp);
        ui_modal_close();
    } else if (opt == 1) { // Add All to Queue
        LoadedPlaylist lp;
        if (playlist_mgmt_load_playlist(s_target_playlist, &lp)) {
            pthread_mutex_lock(&state_mutex);
            for (int i = 0; i < lp.count; i++) {
                if (num_playlist_files >= playlist_capacity) {
                    playlist_capacity = playlist_capacity == 0 ? 1024 : playlist_capacity * 2;
                    playlist = realloc(playlist, sizeof(PlaylistEntry) * playlist_capacity);
                }
                int idx = num_playlist_files++;
                strncpy(playlist[idx].path, lp.items[i].path, sizeof(playlist[idx].path) - 1);
                strncpy(playlist[idx].name, lp.items[i].title[0] ? lp.items[i].title : lp.items[i].path, 255);
                playlist[idx].display_width = utf8_display_width(playlist[idx].name);
                memset(&playlist[idx].meta, 0, sizeof(KoniMetadata));
                playlist[idx].metadata_loaded = false;
                playlist[idx].duration_sec = lp.items[i].duration_sec;
            }
            pthread_mutex_unlock(&state_mutex);
            ui_status_set("Added %d tracks to Queue", lp.count);
        }
        playlist_mgmt_free_loaded(&lp);
        ui_modal_close();
    } else if (opt == 2) { // Rename
        if (strcasecmp(s_target_playlist, "Favourites") == 0) {
            ui_status_set("Cannot rename Favourites");
            ui_modal_close();
        } else {
            open_text_input("Rename Playlist", s_target_playlist, TEXT_INPUT_RENAME_PLAYLIST);
        }
    } else if (opt == 3) { // Delete
        if (strcasecmp(s_target_playlist, "Favourites") == 0) {
            ui_status_set("Cannot delete Favourites");
        } else {
            playlist_mgmt_delete(s_target_playlist);
            ui_status_set("Deleted playlist: %s", s_target_playlist);
        }
        ui_modal_close();
    }
}

bool ui_modal_handle_input(int ch) {
    if (s_modal_type == MODAL_NONE) return false;

    if (ch == 27) { // ESC
        if (s_modal_type == MODAL_ADD_TO_PLAYLIST) {
            // Return back to track actions
            s_modal_type = MODAL_TRACK_ACTIONS;
            s_selected_item = 3;
            force_redraw = true;
            return true;
        }
        ui_modal_close();
        return true;
    }

    if (s_modal_type == MODAL_TEXT_INPUT) {
        if (ch == 10) { // Enter
            if (s_input_len > 0) {
                if (s_input_purpose == TEXT_INPUT_CREATE_PLAYLIST) {
                    bool created = playlist_mgmt_create(s_input_buffer);
                    if (created) {
                        playlist_mgmt_add_track(s_input_buffer, s_track_ctx.path, s_track_ctx.title, s_track_ctx.artist, s_track_ctx.duration_sec);
                        ui_status_set("Created and added to %s", s_input_buffer);
                    } else {
                        ui_status_set("Playlist already exists");
                    }
                } else if (s_input_purpose == TEXT_INPUT_RENAME_PLAYLIST) {
                    playlist_mgmt_rename(s_target_playlist, s_input_buffer);
                    ui_status_set("Renamed to %s", s_input_buffer);
                }
            }
            ui_modal_close();
            return true;
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (s_input_len > 0) {
                s_input_buffer[--s_input_len] = '\0';
                force_redraw = true;
            }
            return true;
        } else if (ch >= 32 && ch <= 126) {
            if (s_input_len < (int)sizeof(s_input_buffer) - 1) {
                s_input_buffer[s_input_len++] = (char)ch;
                s_input_buffer[s_input_len] = '\0';
                force_redraw = true;
            }
            return true;
        }
        return true;
    }

    if (s_modal_type == MODAL_TRACK_DETAILS) {
        if (ch == 10 || ch == 27 || ch == 'q' || ch == ' ') {
            ui_modal_close();
            return true;
        }
        return true;
    }

    // Modal navigation for list types
    int total_options = 0;
    if (s_modal_type == MODAL_TRACK_ACTIONS) total_options = s_track_ctx.in_playlist ? 6 : 5;
    else if (s_modal_type == MODAL_ADD_TO_PLAYLIST) total_options = playlist_mgmt_get_count() + 1;
    else if (s_modal_type == MODAL_PLAYLIST_ACTIONS) total_options = 4;

    if (ch == KEY_UP || ch == 'k') {
        if (s_selected_item > 0) s_selected_item--;
        force_redraw = true;
        return true;
    }

    if (ch == KEY_DOWN || ch == 'j') {
        if (s_selected_item < total_options - 1) s_selected_item++;
        force_redraw = true;
        return true;
    }

    // Number hotkey selection '1'..'9'
    if (ch >= '1' && ch <= '9') {
        int idx = ch - '1';
        if (idx < total_options) {
            s_selected_item = idx;
            if (s_modal_type == MODAL_TRACK_ACTIONS) handle_track_action_select(idx);
            else if (s_modal_type == MODAL_ADD_TO_PLAYLIST) handle_add_to_playlist_select(idx);
            else if (s_modal_type == MODAL_PLAYLIST_ACTIONS) handle_playlist_action_select(idx);
            return true;
        }
    }

    if (ch == 10) { // Enter key selection
        if (s_modal_type == MODAL_TRACK_ACTIONS) handle_track_action_select(s_selected_item);
        else if (s_modal_type == MODAL_ADD_TO_PLAYLIST) handle_add_to_playlist_select(s_selected_item);
        else if (s_modal_type == MODAL_PLAYLIST_ACTIONS) handle_playlist_action_select(s_selected_item);
        return true;
    }

    return true; // Absorb any other keys while modal is open
}