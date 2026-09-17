#define _GNU_SOURCE
#include "sparkles_widgets.h"
#include "state.h"
#include "modals/sparkles_context_menu.h"
#include "input/sparkles_input.h"
#include <string.h>
#include <strings.h>
#include <math.h>

static int s_song_scroll = 0;
static int s_saved_scroll = 0;
static bool s_search_active = false;
static char s_search_buf[64] = {0};
static int s_search_len = 0;
static int s_filtered_indices[65536];
static char s_prev_playing_path[1024] = {0};
static bool s_locate_request = false;

static bool str_contains_ci(const char *haystack, const char *needle) {
    if (!needle || !needle[0]) return true;
    if (!haystack) return false;
    return strcasestr(haystack, needle) != NULL;
}

bool tile_song_list_is_searching(void) {
    return s_search_active;
}

void tile_song_list_open_search(void) {
    s_saved_scroll = s_song_scroll;
    s_search_active = true;
    s_search_buf[0] = '\0';
    s_search_len = 0;
    s_song_scroll = 0;
}

void tile_song_list_close_search(void) {
    s_search_active = false;
    s_search_buf[0] = '\0';
    s_search_len = 0;

    pthread_mutex_lock(&state_mutex);
    int total = num_library_tracks;
    int sel = selected_library_idx;
    pthread_mutex_unlock(&state_mutex);

    if (total > 0 && sel >= 0 && sel < total) {
        int max_rows = 14;
        // If the selector was already within the previous viewport, preserve it
        if (sel >= s_saved_scroll && sel < s_saved_scroll + max_rows) {
            s_song_scroll = s_saved_scroll;
        } else {
            // Otherwise, center or bring the selected track into view
            s_song_scroll = sel - (max_rows / 3);
        }
        if (s_song_scroll > total - max_rows) s_song_scroll = total - max_rows;
        if (s_song_scroll < 0) s_song_scroll = 0;
    } else {
        s_song_scroll = s_saved_scroll;
        if (s_song_scroll < 0) s_song_scroll = 0;
    }
}

void tile_song_list_locate_playing(void) {
    pthread_mutex_lock(&state_mutex);
    if (playing_filepath[0] != '\0' && num_library_tracks > 0) {
        for (int t = 0; t < num_library_tracks; t++) {
            if (strcmp(library_tracks[t].path, playing_filepath) == 0) {
                selected_library_idx = t;
                // If search was filtering this song out, clear it alowing the song be visible
                if (s_search_active) {
                    s_search_active = false;
                    s_search_buf[0] = '\0';
                    s_search_len = 0;
                }
                s_locate_request = true;
                break;
            }
        }
    }
    pthread_mutex_unlock(&state_mutex);
}

void tile_song_list_render(SparklesTile *tile, Rectangle b) {
    (void)tile;
    DrawText("Song List", (int)(b.x + 18), (int)(b.y + 16), FONT_SIZE_LG, COLOR_TEXT_PRIMARY);

    // Right-align search
    if (!s_search_active) {
        int search_btn_w = MeasureText("[Search]", FONT_SIZE_SM);
        DrawText("[Search]", (int)(b.x + b.width - search_btn_w - 20), (int)(b.y + 19), FONT_SIZE_SM, COLOR_ACCENT);
    } else {
        int title_w = MeasureText("Song List", FONT_SIZE_LG);
        float sbox_x = b.x + 18 + title_w + 14;
        float sbox_w = b.width - (sbox_x - b.x) - 44;
        if (sbox_w < 60) sbox_w = 60;
        Rectangle search_box = { sbox_x, b.y + 14, sbox_w, 24 };

        DrawRectangleRounded(search_box, 0.2f, 4, (Color){ 32, 34, 40, 255 });
        DrawRectangleRoundedLinesEx(search_box, 0.2f, 4, 1.0f, COLOR_ACCENT);

        const char *cursor = ((int)(GetTime() * 2.5f) % 2 == 0) ? "_" : " ";
        DrawText(TextFormat("/ %s%s", s_search_buf, cursor), (int)(search_box.x + 8), (int)(search_box.y + 4), FONT_SIZE_SM, COLOR_TEXT_PRIMARY);
        DrawText("[x]", (int)(b.x + b.width - 32), (int)(b.y + 18), FONT_SIZE_SM, COLOR_TEXT_MUTED);
    }

    // Partitioned columns
    float time_w = (float)MeasureText("00:00", FONT_SIZE_SM) + 4.0f;
    float time_x = b.x + b.width - time_w - 18.0f;
    float art_x  = b.x + 64.0f;
    float tit_x  = b.x + (b.width * 0.40f);
    if (tit_x < art_x + 90.0f) tit_x = art_x + 90.0f;

    float art_w = tit_x - art_x - 12.0f;
    float tit_w = time_x - tit_x - 12.0f;

    float hdr_y = b.y + 44;
    DrawText("Play", (int)(b.x + 18), (int)hdr_y, FONT_SIZE_SM, COLOR_TEXT_DARK);
    DrawText("Artist", (int)art_x, (int)hdr_y, FONT_SIZE_SM, COLOR_TEXT_DARK);
    DrawText("Title", (int)tit_x, (int)hdr_y, FONT_SIZE_SM, COLOR_TEXT_DARK);
    DrawText("Time", (int)time_x, (int)hdr_y, FONT_SIZE_SM, COLOR_TEXT_DARK);

    pthread_mutex_lock(&state_mutex);

    // Build filtered indices if search active
    int total_tracks = num_library_tracks;
    int count = 0;
    for (int t = 0; t < total_tracks; t++) {
        if (!s_search_active || s_search_len == 0 ||
            str_contains_ci(library_tracks[t].title, s_search_buf) ||
            str_contains_ci(library_tracks[t].artist, s_search_buf) ||
            str_contains_ci(library_tracks[t].name, s_search_buf)) {
            s_filtered_indices[count++] = t;
        }
    }

    int row_h = 32;
    int max_rows = (int)(b.height - 74) / row_h;

    /* Cursor Rule
       If the playing song changed
       If the cursor was over the previous playing song, advance the cursor to the new song.
       If not, leave the cursor exactly where the user placed it. */
    bool cursor_was_on_song = false;
    if (strcmp(playing_filepath, s_prev_playing_path) != 0) {
        if (s_prev_playing_path[0] != '\0' && current_play_source == SOURCE_LIBRARY) {
            if (selected_library_idx >= 0 && selected_library_idx < num_library_tracks) {
                if (strcmp(library_tracks[selected_library_idx].path, s_prev_playing_path) == 0) {
                    cursor_was_on_song = true;
                }
            }

            if (cursor_was_on_song && playing_filepath[0] != '\0') {
                for (int t = 0; t < num_library_tracks; t++) {
                    if (strcmp(library_tracks[t].path, playing_filepath) == 0) {
                        selected_library_idx = t;
                        break;
                    }
                }
            }
        } else if (s_prev_playing_path[0] == '\0' && playing_filepath[0] != '\0' && current_play_source == SOURCE_LIBRARY) {
            for (int t = 0; t < num_library_tracks; t++) {
                if (strcmp(library_tracks[t].path, playing_filepath) == 0) {
                    selected_library_idx = t;
                    cursor_was_on_song = true;
                    break;
                }
            }
        }
        strncpy(s_prev_playing_path, playing_filepath, sizeof(s_prev_playing_path) - 1);

        // Keep cursor in view if it moved with the song
        if (cursor_was_on_song) {
            for (int k = 0; k < count; k++) {
                if (s_filtered_indices[k] == selected_library_idx) {
                    if (k < s_song_scroll) s_song_scroll = k;
                    else if (k >= s_song_scroll + max_rows) s_song_scroll = k - max_rows + 1;
                    break;
                }
            }
        }
    }

    // Handle Shift+L locate request, center the song in the viewport
    if (s_locate_request) {
        for (int k = 0; k < count; k++) {
            if (s_filtered_indices[k] == selected_library_idx) {
                s_song_scroll = k - (max_rows / 2);
                break;
            }
        }
        s_locate_request = false;
    }

    // Bounds checking on scroll offset
    if (s_song_scroll > count - max_rows) s_song_scroll = count - max_rows;
    if (s_song_scroll < 0) s_song_scroll = 0;

    Vector2 mouse = GetMousePosition();

    for (int i = 0; i < max_rows && (i + s_song_scroll) < count; i++) {
        int target = s_filtered_indices[i + s_song_scroll];
        float y = b.y + 70 + (i * row_h);
        bool is_playing = (playing_filepath[0] != '\0' &&
                           strcmp(library_tracks[target].path, playing_filepath) == 0);
        bool is_cursor = (target == selected_library_idx);

        Rectangle row_bar = { b.x + 8, y - 2, b.width - 16, (float)row_h };
        bool is_hover = CheckCollisionPointRec(mouse, row_bar);

        // Dim white cursor bar over the song
        if (is_cursor) {
            DrawRectangleRec(row_bar, (Color){ 255, 255, 255, 15 });
            DrawRectangle((int)row_bar.x, (int)row_bar.y + 4, 2, (int)row_bar.height - 8, is_playing ? COLOR_ACCENT : (Color){ 220, 225, 235, 180 });
        } else if (is_hover) {
            DrawRectangleRec(row_bar, (Color){ 255, 255, 255, 7 });
        }

        // Play glyph
        DrawText(is_playing ? "||" : ">", (int)(b.x + 22), (int)(y + 2), FONT_SIZE_SM, is_playing ? COLOR_ACCENT : (is_cursor ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED));

        // Artist
        const char *art = library_tracks[target].artist[0] ? library_tracks[target].artist : "Unknown";
        Rectangle art_col = { art_x, y + 2, art_w, (float)FONT_SIZE_MD };
        DrawTextMarquee(art, art_col, b, FONT_SIZE_MD, COLOR_TEXT_MUTED, 24.0f);

        // Title
        const char *tit = library_tracks[target].title[0] ? library_tracks[target].title : library_tracks[target].name;
        Rectangle tit_col = { tit_x, y + 2, tit_w, (float)FONT_SIZE_MD };
        DrawTextMarquee(tit, tit_col, b, FONT_SIZE_MD, is_playing ? COLOR_ACCENT : COLOR_TEXT_PRIMARY, 28.0f);

        // Duration right-aligned
        uint32_t dur = library_tracks[target].duration_sec;
        DrawText(TextFormat("%u:%02u", dur / 60, dur % 60), (int)time_x, (int)(y + 3), FONT_SIZE_SM, COLOR_TEXT_DARK);
    }

    // Scrollbar indicator
    if (count > max_rows && max_rows > 0) {
        float track_h = b.height - 74.0f;
        float thumb_h = fmaxf(14.0f, track_h * ((float)max_rows / (float)count));
        float thumb_y = b.y + 70.0f + ((float)s_song_scroll / (float)(count - max_rows)) * (track_h - thumb_h);
        DrawRectangle((int)(b.x + b.width - 8), (int)thumb_y, 4, (int)thumb_h, ColorAlpha(COLOR_ACCENT, 0.4f));
    }

    pthread_mutex_unlock(&state_mutex);
}

void tile_song_list_input(SparklesTile *tile, Rectangle b) {
    (void)tile;
    Vector2 m = GetMousePosition();

    // Keyboard navigation when not typing in search
    if (!s_search_active && CheckCollisionPointRec(m, b)) {
        int nav_dir = 0;
        if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_K)) nav_dir = -1;
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_J)) nav_dir = 1;

        if (nav_dir != 0 && num_library_tracks > 0) {
            // Find current filtered position of cursor
            int cur_pos = -1;
            int count = 0;
            for (int t = 0; t < num_library_tracks; t++) {
                if (s_search_len == 0 ||
                    str_contains_ci(library_tracks[t].title, s_search_buf) ||
                    str_contains_ci(library_tracks[t].artist, s_search_buf) ||
                    str_contains_ci(library_tracks[t].name, s_search_buf)) {
                    if (t == selected_library_idx) cur_pos = count;
                    count++;
                }
            }

            int next_pos = cur_pos + nav_dir;
            if (next_pos < 0) next_pos = 0;
            if (next_pos >= count) next_pos = count - 1;

            if (next_pos >= 0 && next_pos < count) {
                selected_library_idx = s_filtered_indices[next_pos];

                int row_h = 30;
                int max_rows = (int)(b.height - 74) / row_h;
                if (next_pos < s_song_scroll) s_song_scroll = next_pos;
                else if (next_pos >= s_song_scroll + max_rows) s_song_scroll = next_pos - max_rows + 1;
            }
        }

        if (IsKeyPressed(KEY_ENTER) && selected_library_idx >= 0 && selected_library_idx < num_library_tracks) {
            pthread_mutex_lock(&state_mutex);
            strncpy(playing_filepath, library_tracks[selected_library_idx].path, sizeof(playing_filepath) - 1);
            strncpy(playing_filename, library_tracks[selected_library_idx].name, 255);
            playing_file_idx = selected_library_idx;
            current_play_source = SOURCE_LIBRARY;
            pthread_mutex_unlock(&state_mutex);

            atomic_store(&seek_target_ms, -1);
            atomic_store(&current_cmd_atomic, CMD_PLAY);
        }
    }

    // Keyboard Text Input when search is open
    if (s_search_active) {
        // Allow Up/Down arrow navigation through filtered results while searching
        int nav_dir = 0;
        if (IsKeyPressed(KEY_UP)) nav_dir = -1;
        if (IsKeyPressed(KEY_DOWN)) nav_dir = 1;

        if (nav_dir != 0 && num_library_tracks > 0) {
            pthread_mutex_lock(&state_mutex);
            int count = 0;
            int cur_pos = -1;
            for (int t = 0; t < num_library_tracks; t++) {
                if (s_search_len == 0 ||
                    str_contains_ci(library_tracks[t].title, s_search_buf) ||
                    str_contains_ci(library_tracks[t].artist, s_search_buf) ||
                    str_contains_ci(library_tracks[t].name, s_search_buf)) {
                    if (t == selected_library_idx) cur_pos = count;
                    count++;
                }
            }

            int next_pos = cur_pos + nav_dir;
            if (next_pos < 0) next_pos = 0;
            if (next_pos >= count) next_pos = count - 1;

            if (next_pos >= 0 && next_pos < count) {
                selected_library_idx = s_filtered_indices[next_pos];

                int row_h = 32;
                int max_rows = (int)(b.height - 74) / row_h;
                if (next_pos < s_song_scroll) s_song_scroll = next_pos;
                else if (next_pos >= s_song_scroll + max_rows) s_song_scroll = next_pos - max_rows + 1;
            }
            pthread_mutex_unlock(&state_mutex);
        }

        if (IsKeyPressed(KEY_ENTER) && selected_library_idx >= 0 && selected_library_idx < num_library_tracks) {
            pthread_mutex_lock(&state_mutex);
            strncpy(playing_filepath, library_tracks[selected_library_idx].path, sizeof(playing_filepath) - 1);
            strncpy(playing_filename, library_tracks[selected_library_idx].name, 255);
            playing_file_idx = selected_library_idx;
            current_play_source = SOURCE_LIBRARY;
            pthread_mutex_unlock(&state_mutex);

            atomic_store(&seek_target_ms, -1);
            atomic_store(&current_cmd_atomic, CMD_PLAY);
            tile_song_list_close_search();
            return;
        }

        int c = GetCharPressed();
        while (c > 0) {
            if (c >= 32 && c <= 126 && s_search_len < (int)sizeof(s_search_buf) - 1) {
                s_search_buf[s_search_len++] = (char)c;
                s_search_buf[s_search_len] = '\0';
                s_song_scroll = 0;
            }
            c = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) && s_search_len > 0) {
            s_search_buf[--s_search_len] = '\0';
            s_song_scroll = 0;
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            tile_song_list_close_search();
            return;
        }
    }

    // Drag & wheel scroll
    float scroll = sparkles_input_get_scroll_delta(b);
    if (scroll != 0.0f) {
        s_song_scroll += (int)roundf(scroll);
        if (s_song_scroll < 0) s_song_scroll = 0;
    }

    // Long-press or right-click context menu
    Vector2 lp;
    if (sparkles_input_consume_long_press(b, &lp)) {
        if (lp.y >= b.y + 70.0f) {
            int clicked = s_song_scroll + (int)(lp.y - (b.y + 70.0f)) / 32;
            pthread_mutex_lock(&state_mutex);
            int match_count = 0;
            for (int t = 0; t < num_library_tracks; t++) {
                if (!s_search_active || s_search_len == 0 ||
                    str_contains_ci(library_tracks[t].title, s_search_buf) ||
                    str_contains_ci(library_tracks[t].artist, s_search_buf) ||
                    str_contains_ci(library_tracks[t].name, s_search_buf)) {
                    if (match_count == clicked) {
                        SparklesTrackItem item = {0};
                        strncpy(item.path, library_tracks[t].path, sizeof(item.path) - 1);
                        strncpy(item.title, library_tracks[t].title[0] ? library_tracks[t].title : library_tracks[t].name, sizeof(item.title) - 1);
                        strncpy(item.artist, library_tracks[t].artist, sizeof(item.artist) - 1);
                        strncpy(item.album, library_tracks[t].album, sizeof(item.album) - 1);
                        item.duration_sec = library_tracks[t].duration_sec;
                        pthread_mutex_unlock(&state_mutex);
                        sparkles_context_menu_open(lp, &item);
                        return;
                    }
                    match_count++;
                }
            }
            pthread_mutex_unlock(&state_mutex);
            return;
        }
    }

    // Tap or left-click
    Vector2 tap;
    if (sparkles_input_consume_tap(b, &tap)) {
        int search_btn_w = MeasureText("[Search]", FONT_SIZE_SM);
        if (!s_search_active && CheckCollisionPointRec(tap, (Rectangle){ b.x + b.width - search_btn_w - 24, b.y + 12, search_btn_w + 12, 26 })) {
            s_search_active = true;
            return;
        }
        if (s_search_active && CheckCollisionPointRec(tap, (Rectangle){ b.x + b.width - 38, b.y + 12, 28, 25 })) {
            tile_song_list_close_search();
            return;
        }

        if (tap.y >= b.y + 70.0f) {
            int clicked = s_song_scroll + (int)(tap.y - (b.y + 70.0f)) / 32;
            pthread_mutex_lock(&state_mutex);
            int match_count = 0;
            for (int t = 0; t < num_library_tracks; t++) {
                if (!s_search_active || s_search_len == 0 ||
                    str_contains_ci(library_tracks[t].title, s_search_buf) ||
                    str_contains_ci(library_tracks[t].artist, s_search_buf) ||
                    str_contains_ci(library_tracks[t].name, s_search_buf)) {
                    if (match_count == clicked) {
                        selected_library_idx = t;
                        strncpy(playing_filepath, library_tracks[t].path, sizeof(playing_filepath) - 1);
                        strncpy(playing_filename, library_tracks[t].name, 255);
                        playing_file_idx = t;
                        current_play_source = SOURCE_LIBRARY;
                        pthread_mutex_unlock(&state_mutex);

                        atomic_store(&seek_target_ms, -1);
                        atomic_store(&current_cmd_atomic, CMD_PLAY);
                        return;
                    }
                    match_count++;
                }
            }
            pthread_mutex_unlock(&state_mutex);
        }
    }
}